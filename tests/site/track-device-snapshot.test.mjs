import test from 'node:test';
import assert from 'node:assert/strict';
import {readDeviceTrackSnapshot,RECORDING_FORMAT} from '../../site/tracks/src/device-snapshot.js';
import {OPERATION,encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
const id=new Uint8Array(16).fill(7),source=Uint8Array.from({length:700},(_,index)=>index%251);
const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
test('maximum snapshot response fits the existing 512-byte management frame',()=>{
 const body=[id,8*1024*1024,hash,3,new Uint8Array(320),8*1024*1024-320];
 const encoded=encodeManagement(id,new Uint8Array(16).fill(9),0,body);
 assert.ok(encoded.length<=512);assert.deepEqual(decodeManagement(encoded).body,body);
 const request=encodeManagement(id,new Uint8Array(16).fill(9),81,[id,8*1024*1024-320,320,hash]);assert.ok(request.length<=512);
});
function session(adapt=value=>value){const calls=[];return {calls,async request(operation,body){calls.push({operation,body});const [file,offset,length]=body;return adapt([file.slice(),source.length,hash.slice(),RECORDING_FORMAT.CSV,source.slice(offset,offset+length),offset],calls.length,body);}};}
test('snapshot reads bounded chunks with frozen ID/hash and actual whole-file SHA',async()=>{
 const device=session(),progress=[],input=id.slice();const read=readDeviceTrackSnapshot(device,input,{onProgress:value=>progress.push(value)});input.fill(0);const result=await read;
 assert.deepEqual(result.source,source);assert.deepEqual(result.sha256,hash);assert.deepEqual(result.snapshotId,id);assert.equal(result.format,2);
 assert.deepEqual(device.calls.map(call=>call.body[1]),[0,320,640]);assert.ok(device.calls.every(call=>call.operation===OPERATION.TRACK_SNAPSHOT&&call.body[2]===320));
 assert.equal(device.calls[0].body[3],null);assert.deepEqual(device.calls[1].body[3],hash);assert.deepEqual(progress.map(value=>value.offset),[320,640,700]);assert.ok(progress.every(Object.isFrozen));
});
test('response rejects changed identity, length, hash, format, empty/oversized chunk and corrupt content',async()=>{
 for(const mutate of [v=>v[0].fill(8),v=>v[1]++,v=>v[2].fill(2),v=>v[3]=3,v=>v[4]=new Uint8Array(),v=>v[4]=new Uint8Array(321),v=>v[5]++]){
  await assert.rejects(readDeviceTrackSnapshot(session((value,count)=>{if(count===2)mutate(value);return value;}),id),/响应无效|发生变化/);
 }
 await assert.rejects(readDeviceTrackSnapshot(session(value=>{value[4][0]^=1;return value;}),id),/哈希不匹配/);
 await assert.rejects(readDeviceTrackSnapshot(session(value=>{value[1]=8*1024*1024+1;return value;}),id),/响应无效/);
});
test('short chunks advance exact offsets and terminal overshoot is rejected',async()=>{
 const device=session((value,count,body)=>{value[4]=source.slice(body[1],body[1]+73);return value;});assert.deepEqual((await readDeviceTrackSnapshot(device,id)).source,source);assert.equal(device.calls[1].body[1],73);
 await assert.rejects(readDeviceTrackSnapshot(session((value,count)=>{if(count===3)value[4]=new Uint8Array(61);return value;}),id),/发生变化/);
});
test('busy retries only current offset, has deadline and abort releases session',async()=>{
 const device=session((value,count)=>{if(count===1)throw Object.assign(Error('busy'),{code:'BUSY'});return value;});await readDeviceTrackSnapshot(device,id);assert.deepEqual(device.calls.slice(0,2).map(call=>call.body[1]),[0,0]);
 const busy=session(()=>{throw Object.assign(Error('busy'),{code:'BUSY'});});await assert.rejects(readDeviceTrackSnapshot(busy,id,{busyTimeoutMs:5}),/持续忙碌/);
 const abort=new AbortController(),pending=readDeviceTrackSnapshot(busy,id,{signal:abort.signal});abort.abort();await assert.rejects(pending,error=>error.name==='AbortError');
 busy.request=session().request;assert.deepEqual((await readDeviceTrackSnapshot(busy,id)).source,source);
});
test('concurrent read is rejected; late response after cancellation is not accepted',async()=>{
 let finish;const device={request:()=>new Promise(resolve=>{finish=resolve;})},abort=new AbortController();const pending=readDeviceTrackSnapshot(device,id,{signal:abort.signal});
 await assert.rejects(readDeviceTrackSnapshot(device,id),/已有轨迹读取/);abort.abort();finish([id,source.length,hash,2,source.slice(0,320),0]);await assert.rejects(pending,error=>error.name==='AbortError');
 await assert.rejects(readDeviceTrackSnapshot(session(),new Uint8Array(16)),/参数无效/);
});
test('numeric firmware Busy status retries the current snapshot offset',async()=>{
 const device=session((value,count)=>{if(count===1)throw Object.assign(Error('busy'),{code:5});return value;});assert.deepEqual((await readDeviceTrackSnapshot(device,id)).source,source);assert.deepEqual(device.calls.slice(0,2).map(call=>call.body[1]),[0,0]);
});
