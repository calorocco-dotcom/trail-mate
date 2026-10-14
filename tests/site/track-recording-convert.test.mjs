import test from 'node:test';
import assert from 'node:assert/strict';
import {convertRecording} from '../../site/tracks/src/recording-convert.js';
const encode=text=>new TextEncoder().encode(text),text=result=>new TextDecoder().decode(result.kml);
test('Arduino CSV preserves unavailable height, UTC and raw invalid-clock counter',()=>{
 const input=encode('lat,lon,ts,sat\n30,120,1700000000,9\n30.1,120.1,2,3\n');const copy=input.slice();
 const result=convertRecording(input,{format:'csv',title:'A<&B'}),xml=text(result);
 assert.deepEqual(input,copy);assert.deepEqual(result.source,copy);assert.equal(result.pointCount,2);
 assert.match(xml,/<name>A&lt;&amp;B<\/name>/);assert.match(xml,/<coordinates>120,30 120.1,30.1<\/coordinates>/);
 assert.match(xml,/2023-11-14T22:13:20.000Z/);assert.match(xml,/timestamp_s&quot;:2/);assert.doesNotMatch(xml,/<gx:Track>|<altitudeMode>/);
 assert.ok(result.issues.some(issue=>issue.includes('UTC')));
});
test('IDF CSV retains ambiguous zero height/speed without claiming known altitude',()=>{
 const result=convertRecording(encode('time,lat,lon,alt_m,speed_mps,satellites\n2024-01-02T03:04:05Z,30,120,0,0,4\n,31,121,50,2.5,5\n'),{format:'csv'});
 const xml=text(result);assert.match(xml,/recorded_alt_m&quot;:50/);assert.match(xml,/recorded_speed_mps&quot;:2.5/);
 assert.match(xml,/<coordinates>120,30 121,31<\/coordinates>/);assert.doesNotMatch(xml,/<altitudeMode>/);
 assert.throws(()=>convertRecording(encode('time,lat,lon,alt_m,speed_mps,satellites\n2024-02-30T00:00:00Z,30,120,0,0,4\n'),{format:'csv'}),/时间/);
});
function binary(header,stride,count){const bytes=new Uint8Array(header.length+stride*count);bytes.set(encode(header));return {bytes,view:new DataView(bytes.buffer),start:header.length};}
test('Arduino TRK1 uses little endian 16-byte padded records',()=>{
 const {bytes,view,start}=binary('TRK1',16,2);
 for(let i=0;i<2;i++){const offset=start+i*16;view.setInt32(offset,-300000000,true);view.setInt32(offset+4,1799000000,true);view.setUint32(offset+8,1700000000+i,true);view.setUint8(offset+12,8);bytes.fill(0xaa,offset+13,offset+16);}
 const xml=text(convertRecording(bytes,{format:'binary'}));assert.match(xml,/<coordinates>179.9,-30 179.9,-30<\/coordinates>/);assert.match(xml,/satellites&quot;:8/);
 assert.throws(()=>convertRecording(bytes.subarray(0,bytes.length-1),{format:'binary'}),/截断/);
});
test('IDF TMTRK1 uses 20-byte stride, valid flags and gx time with known elevation',()=>{
 const {bytes,view,start}=binary('TMTRK1',20,2);
 for(let i=0;i<2;i++){const offset=start+i*20;view.setUint32(offset,1700000000+i,true);view.setInt32(offset+4,300000000,true);view.setInt32(offset+8,-1799000000,true);view.setInt16(offset+12,-5+i,true);view.setUint16(offset+14,250,true);view.setUint8(offset+16,10);view.setUint8(offset+17,3);bytes.fill(0x55,offset+18,offset+20);}
 const result=convertRecording(bytes,{format:'binary'}),xml=text(result);assert.match(xml,/<gx:Track><altitudeMode>absolute/);assert.match(xml,/<gx:coord>-179.9 30 -5<\/gx:coord>/);assert.match(xml,/speed_mps&quot;:2.5/);
 bytes[start+17]=0;const mixed=text(convertRecording(bytes,{format:'binary'}));assert.doesNotMatch(mixed,/<gx:Track>|<altitudeMode>/);assert.match(mixed,/<coordinates>-179.9,30 -179.9,30,-4<\/coordinates>/);
 bytes[start+17]=4;assert.throws(()=>convertRecording(bytes,{format:'binary'}),/未知有效标记/);
});
test('conversion rejects invalid coordinates, columns, empty files and budgets',()=>{
 for(const row of ['91,120,1,3','30,181,1,3','NaN,120,1,3','30,120,-1,3','30,120,1,256','30,120,1'])assert.throws(()=>convertRecording(encode('lat,lon,ts,sat\n'+row+'\n'),{format:'csv'}));
 assert.throws(()=>convertRecording(encode('lat,lon,ts,sat\n'),{format:'csv'}),/没有坐标/);
 assert.throws(()=>convertRecording(encode('lat,lon,ts,sat\n30,120,1,3\n\n31,121,2,3'),{format:'csv'}),/空行/);
 assert.throws(()=>convertRecording(new Uint8Array(8*1024*1024+1),{format:'binary'}),/容量/);
 const {bytes}=binary('TRK1',16,100001);assert.throws(()=>convertRecording(bytes,{format:'binary'}),/点数量/);
});
