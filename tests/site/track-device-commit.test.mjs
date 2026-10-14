import test from 'node:test';import assert from 'node:assert/strict';
import {commitTrack} from '../../site/tracks/src/device-commit.js';
const bin=(size,value)=>new Uint8Array(size).fill(value);
const fixture=()=>({prepared:{prepared:true,operationId:bin(16,1),objectId:bin(32,2),revisionHash:bin(32,3),generation:7,revision:2},
 directory:{capabilitiesVerified:true,protocol:'trailmate.track.v1',delivery:bin(16,4),epoch:bin(16,5)},author:{verified:true,publicKey:bin(64,6),generation:7}});
const receipt=f=>[f.prepared.operationId,f.prepared.objectId,f.prepared.revisionHash,7,2,f.directory.delivery,f.directory.epoch,1];
test('Busy retries preserve exact bindings and admission does not mean published',async()=>{
 const f=fixture(),original=fixture();let calls=0;
 const result=await commitTrack({async request(op,body){assert.equal(op,83);assert.deepEqual(body,receipt(original).slice(0,7));
  body[0].fill(9);f.directory.epoch.fill(9);if(!calls++){const e=Error('busy');e.code=5;throw e;}return receipt(original);}},f.prepared,f.directory,f.author,{sleep:async()=>{}});
 assert.equal(calls,2);assert.equal(result.localAdmitted,true);assert.equal(result.published,false);assert.equal(result.directoryConfirmed,false);
});
test('invalid or mismatched receipt leaves operation unresolved',async()=>{
 for(const mutate of [r=>r.pop(),r=>r[0].fill(8),r=>r[1].fill(8),r=>r[2].fill(8),r=>r[3]=8,r=>r[4]=3,r=>r[5].fill(8),r=>r[6].fill(8),r=>r[7]=0]){
  const f=fixture(),reply=receipt(f).map(v=>v instanceof Uint8Array?v.slice():v);mutate(reply);
  await assert.rejects(commitTrack({async request(){return reply;}},f.prepared,f.directory,f.author),e=>e.code==='COMMIT_PENDING'&&e.operationId[0]===1);
 }
});
test('lost response preserves operation and preflight rejects stale identity without traffic',async()=>{
 const f=fixture();await assert.rejects(commitTrack({async request(){throw Error('disconnected');}},f.prepared,f.directory,f.author),e=>e.code==='COMMIT_PENDING'&&e.cause.message==='disconnected');
 await assert.rejects(commitTrack({async request(){assert.fail('unexpected traffic');}},f.prepared,f.directory,{...f.author,generation:8}),/设备身份/);
});
