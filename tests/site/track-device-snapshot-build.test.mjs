import test from 'node:test';
import assert from 'node:assert/strict';
import {buildDeviceTrackSnapshot} from '../../site/tracks/src/device-snapshot-build.js';
const id=new Uint8Array(16).fill(3),entry={name:'山径.gpx',bytes:750n,format:1,recording:false};
test('build binds selection, copies ID and waits for sealed full result',async()=>{
 const calls=[],progress=[];let poll=0;
 const device={async request(op,body){calls.push([op,body]);assert.equal(op,86);assert.deepEqual(body,[id,7,'山径.gpx',750,1]);return [body[0],++poll===4?3:1,Math.min(poll*320,750),750,1];}};
 const input=id.slice();const promise=buildDeviceTrackSnapshot(device,entry,7,{snapshotId:input,pollIntervalMs:0,onProgress:value=>progress.push(value)});input.fill(0);const result=await promise;
 assert.deepEqual(result.snapshotId,id);assert.equal(result.bytes,750);assert.equal(calls.length,4);assert.equal(progress.at(-1).state,3);
});
test('numeric Busy retries; cancellation discards late result and requests cleanup',async()=>{
 let polls=0,cancelled=0;const abort=new AbortController();
 const device={async request(op,body){if(op===87){cancelled++;assert.deepEqual(body,[id]);return [];}if(++polls===1)throw Object.assign(Error('busy'),{code:5});abort.abort();return [id,3,750,750,1];}};
 await assert.rejects(buildDeviceTrackSnapshot(device,entry,7,{snapshotId:id,signal:abort.signal,pollIntervalMs:0}),error=>error.name==='AbortError');assert.equal(polls,2);assert.equal(cancelled,1);
});
test('invalid and regressing states never produce a ready snapshot',async()=>{
 for(const response of [undefined,[id,3,320,750,1],[id,4,0,750,1],[new Uint8Array(16),1,0,750,1],[id,1,0,751,1]]){
  const calls=[];await assert.rejects(buildDeviceTrackSnapshot({async request(op){calls.push(op);return response;}},entry,7,{snapshotId:id,pollIntervalMs:0}));assert.deepEqual(calls,[86,87]);
 }
 let count=0;await assert.rejects(buildDeviceTrackSnapshot({async request(op){return op===87?[]:[id,1,++count===1?320:0,750,1];}},entry,7,{snapshotId:id,pollIntervalMs:0}),/状态无效/);
});
test('live recording, control files, oversized files reject before sending',async()=>{
 for(const change of [{recording:true},{name:'ACTIVE.BIN',format:3},{bytes:8388609n},{name:'../a.gpx'},{name:'a'.repeat(118)+'.gpx'},{format:2}])await assert.rejects(buildDeviceTrackSnapshot({async request(){assert.fail('sent invalid selection');}},{...entry,...change},7));
});
test('no-progress deadline cancels and frees same-session slot',async()=>{
 let cancelled=0;const device={async request(op){if(op===87){cancelled++;return [];}return [id,1,0,750,1];}};
 await assert.rejects(buildDeviceTrackSnapshot(device,entry,7,{snapshotId:id,pollIntervalMs:1,idleTimeoutMs:2}),/无进展/);assert.equal(cancelled,1);
 device.request=async()=>[id,3,750,750,1];assert.equal((await buildDeviceTrackSnapshot(device,entry,7,{snapshotId:id})).bytes,750);
});
