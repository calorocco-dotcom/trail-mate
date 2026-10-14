import test from 'node:test';import assert from 'node:assert/strict';
import {cancelTrackJob} from '../../site/tracks/src/cancel-job.js';
const bin=(n,v)=>new Uint8Array(n).fill(v);
const binding=()=>({operationId:bin(16,1),objectId:bin(32,2),revisionHash:bin(32,3),generation:7,revision:1,directory:bin(16,4),directoryEpoch:bin(16,5),fileHash:bin(32,6),fileBytes:100,authorHash:bin(32,7)});
const reply=b=>[b.operationId,b.objectId,b.revisionHash,7,1,b.directory,b.directoryEpoch,b.fileHash,100,40,6,3,b.authorHash,0];
test('Busy cancellation repeats original operation and validates durable cancelled result',async()=>{
 const b=binding(),original=binding();let calls=0;
 const result=await cancelTrackJob({async request(op,body){assert.equal(op,50);assert.deepEqual(body,[original.operationId,7,1]);b.operationId.fill(9);
  if(!calls++){const error=Error('busy');error.code=5;throw error;}return reply(original);}},b,{sleep:async()=>{}});
 assert.equal(result.cancelled,true);assert.equal(result.published,false);assert.equal(calls,2);
});
test('wrong status, active flag and altered evidence never report cancellation',async()=>{
 for(const change of [r=>r[10]=1,r=>r[13]=1,r=>r[7]=bin(32,9),r=>r[12]=bin(32,9)]){
  const b=binding(),r=reply(b);change(r);await assert.rejects(cancelTrackJob({async request(){return r;}},b),e=>e.code==='JOB_CANCEL_PENDING'&&e.operationId[0]===1);
 }
});
test('disconnect preserves original operation and pre-request abort sends nothing',async()=>{
 const b=binding();await assert.rejects(cancelTrackJob({async request(){throw Error('disconnected');}},b),e=>e.code==='JOB_CANCEL_PENDING'&&e.operationId[0]===1);
 const controller=new AbortController();controller.abort();await assert.rejects(cancelTrackJob({async request(){assert.fail('unexpected command');}},b,{signal:controller.signal}),e=>e.name==='AbortError');
});
