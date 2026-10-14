import test from 'node:test';import assert from 'node:assert/strict';
import {waitTrackJob} from '../../site/tracks/src/wait-track-job.js';
const bin=(n,v)=>new Uint8Array(n).fill(v);
const binding=()=>({operationId:bin(16,1),objectId:bin(32,2),revisionHash:bin(32,3),generation:7,revision:2,directory:bin(16,4),directoryEpoch:bin(16,5)});
const reply=(b,phase=1,sequence=2,offset=40,active=1)=>[b.operationId,b.objectId,b.revisionHash,7,2,b.directory,b.directoryEpoch,bin(32,6),100,offset,phase,sequence,bin(32,7),active];
test('Busy and progress wait use the same JobGet binding until directory confirmation',async()=>{
  const b=binding(),original=binding();let calls=0;const progress=[];
  const result=await waitTrackJob({async request(op,body){assert.equal(op,48);assert.deepEqual(body,[original.operationId,7,1]);
    b.operationId.fill(9);if(!calls++){const e=Error('busy');e.code=5;throw e;}
    return calls===2?reply(original):reply(original,4,3,100);}},b,{sleep:async()=>{},onProgress:job=>{progress.push(job.phase);job.fileHash.fill(9);}});
  assert.equal(result.published,true);assert.equal(result.fileHash[0],6);assert.deepEqual(progress,['uploading','confirmed']);
});
test('inactive retained task stops waiting without reporting published',async()=>{
  const b=binding();const result=await waitTrackJob({async request(){return reply(b,1,2,40,0);}},b);
  assert.equal(result.active,false);assert.equal(result.published,false);
});
test('regression and immutable evidence changes leave the original task unresolved',async()=>{
  for(const change of [r=>r[11]=1,r=>r[9]=39,r=>r[7]=bin(32,8),r=>r[12]=bin(32,8),r=>{r[9]=0;r[10]=0;}]){
    const b=binding();let calls=0;
    await assert.rejects(waitTrackJob({async request(){const r=reply(b);if(calls++)change(r);return r;}},b,{sleep:async()=>{}}),
      e=>e.code==='JOB_PENDING'&&e.operationId[0]===1&&e.lastJob.offset===40);
  }
});
test('disconnect preserves the original operation and does not issue another command',async()=>{
  const b=binding();await assert.rejects(waitTrackJob({async request(op){assert.equal(op,48);throw Error('disconnected');}},b),
    e=>e.code==='JOB_PENDING'&&e.operationId[0]===1&&e.cause.message==='disconnected');
});
test('abort and timeout retain operation ID without cancelling the device',async()=>{
  const b=binding(),controller=new AbortController();controller.abort();
  await assert.rejects(waitTrackJob({async request(){assert.fail('unexpected traffic');}},b,{signal:controller.signal}),
    e=>e.code==='JOB_WAIT_PAUSED'&&e.operationId[0]===1);
  let time=0;
  await assert.rejects(waitTrackJob({async request(){return reply(b);}},b,{timeoutMs:10,pollMs:10,now:()=>time,sleep:async ms=>{time+=ms;}}),
    e=>e.code==='JOB_PENDING'&&e.lastJob.offset===40);
});
