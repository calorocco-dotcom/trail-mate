import test from 'node:test';import assert from 'node:assert/strict';
import {readTrackJob} from '../../site/tracks/src/device-job.js';
const bin=(n,v)=>new Uint8Array(n).fill(v);
const binding=()=>({operationId:bin(16,1),objectId:bin(32,2),revisionHash:bin(32,3),generation:7,revision:2,directory:bin(16,4),directoryEpoch:bin(16,5)});
const response=(b,phase=1)=>[b.operationId,b.objectId,b.revisionHash,7,2,b.directory,b.directoryEpoch,bin(32,6),100,phase===1?40:100,phase,2,bin(32,7),1];
test('JobGet selects tracks and only directory confirmed phase is published',async()=>{
  for(const phase of [1,2,3,4]){
    const b=binding();const job=await readTrackJob({async request(op,body){assert.equal(op,48);assert.deepEqual(body,[b.operationId,7,1]);return response(b,phase);}},b);
    assert.equal(job.published,phase===4);assert.equal(job.directoryConfirmed,phase===4);
  }
});
test('mismatched binding and impossible progress are rejected',async()=>{
  for(const mutate of [r=>r.pop(),r=>r[0]=bin(16,9),r=>r[3]=8,r=>r[6]=bin(16,9),r=>r[9]=101,r=>r[10]=4,r=>r[11]=0,r=>r[13]=2]){
    const b=binding(),r=response(b);mutate(r);await assert.rejects(readTrackJob({async request(){return r;}},b),/状态或绑定无效/);
  }
});
test('query failure stays unresolved and caller mutation does not alter binding',async()=>{
  const b=binding(),original=binding();await assert.rejects(readTrackJob({async request(){throw Error('expired');}},b),/expired/);
  const result=await readTrackJob({async request(){b.directoryEpoch.fill(9);return response(original);}},b);
  assert.deepEqual(result.directoryEpoch,original.directoryEpoch);
});
test('retained file and author evidence must match the first response',async()=>{
 for(const mutate of [r=>r[7]=bin(32,9),r=>r[8]=101,r=>r[12]=bin(32,9)]){
  const b={...binding(),fileHash:bin(32,6),fileBytes:100,authorHash:bin(32,7)},r=response(b);mutate(r);
  await assert.rejects(readTrackJob({async request(){return r;}},b),/状态或绑定无效/);
 }
 const b={...binding(),fileHash:bin(32,6)};
 await assert.rejects(readTrackJob({async request(){assert.fail('unexpected traffic');}},b),/文件或作者绑定无效/);
});
