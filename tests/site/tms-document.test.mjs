import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
const encode=s=>new TextEncoder().encode(s);
const base='TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:%E5%B1%B1%E9%87%8E\ngps.enabled=bool:1\ngps.interval_ms=u32:60000\nrt.primary_psk=b64:AAEC/w==\nrt.bandwidth_khz=f32:125\nfuture.value=opaque:raw-bytes\nEND\n';
test('typed configuration preserves original document bytes and unknown extensions',()=>{
 const bytes=encode(base.replaceAll('\n','\r\n')),d=new TmsDocument(bytes);
 assert.equal(d.get('device.node_name').value,'山野');assert.equal(d.get('gps.enabled').value,true);
 assert.deepEqual(d.get('rt.primary_psk').value,new Uint8Array([0,1,2,255]));assert.deepEqual(d.serialize(),bytes);
 d.set('device.node_name','山野 100%');d.set('gps.enabled',false);
 const text=new TextDecoder().decode(d.serialize());assert.ok(text.includes('future.value=opaque:raw-bytes'));
 assert.equal(new TmsDocument(d.serialize()).get('device.node_name').value,'山野 100%');
 assert.throws(()=>d.set('future.value','new'));assert.throws(()=>d.set('schema.version',8));
});
test('backup conversion is explicit and does not mutate original kind',()=>{
 const d=new TmsDocument(encode(base));const backup=new TmsDocument(d.serialize({kind:'backup'}));
 assert.equal(backup.kind,'backup');assert.equal(d.kind,'working');
});
test('malformed and ambiguous imports fail before editing',()=>{
 for(const text of [base.replace('TMSET7','TMSET8'),base.replace('u16:7','u16:6'),base.replace('END\n',''),base+'junk\n',base.replace('bool:1','bool:true'),base.replace('u32:60000','u32:-1'),base.replace('AAEC/w==','AAEC/x=='),base.replace('%E5%B1%B1%E9%87%8E','%C0%AF'),base.replace('END','gps.enabled=bool:0\nEND')])assert.throws(()=>new TmsDocument(encode(text)));
});
test('type, float and firmware line budgets are enforced',()=>{
 const d=new TmsDocument(encode(base));assert.throws(()=>d.set('gps.enabled',1));assert.throws(()=>d.set('gps.interval_ms',4294967296));assert.throws(()=>d.set('rt.bandwidth_khz',Infinity));assert.throws(()=>d.set('device.node_name','山'.repeat(100)));
 const returned=d.get('rt.primary_psk').value;returned[0]=99;assert.equal(d.get('rt.primary_psk').value[0],0);
});
