import test from 'node:test';
import assert from 'node:assert/strict';
import {prepareTrack} from '../../site/tracks/src/device-prepare.js';
const bin=(size,value)=>new Uint8Array(size).fill(value);
const fixture=()=>({operationId:bin(16,1),kmlUploadId:bin(16,2),manifestUploadId:bin(16,3),generation:7,
  objectId:bin(32,4),revisionHash:bin(32,5),revision:1,parentHash:null});
const ready=f=>[f.operationId.slice(),1,f.objectId.slice(),f.revisionHash.slice(),f.revision];

test('polls Busy and pending with frozen bindings, reports signed rather than published',async()=>{
  const f=fixture(),original=fixture();let calls=0;const phases=[];
  const session={async request(op,body){
    assert.equal(op,82);assert.deepEqual(body,[original.operationId,original.kmlUploadId,original.manifestUploadId,7,null]);
    body[0].fill(9);f.objectId.fill(9);f.operationId.fill(9);
    if(calls++===0){const e=Error('busy');e.code=5;throw e;}
    return calls===2?[original.operationId,0,null,null,0]:ready(original);
  }};
  const result=await prepareTrack(session,f,{sleep:async()=>{},onProgress:p=>phases.push(p.phase)});
  assert.equal(calls,3);assert.deepEqual(phases,['checking','prepared']);assert.equal(result.published,false);
  assert.deepEqual(result.objectId,original.objectId);
});
test('rejects correlation, content and pending-state mismatches',async()=>{
  await assert.rejects(prepareTrack({async request(){}},fixture()),/回执无效/);
  for(const mutate of [r=>r.pop(),r=>r[0].fill(8),r=>r[1]=2,r=>r[2].fill(8),r=>r[3].fill(8),r=>r[4]=2,
    r=>{r[1]=0;},r=>{r[1]=0;r[2]=null;r[3]=null;r[4]=1;}]){
    const f=fixture(),reply=ready(f);mutate(reply);
    await assert.rejects(prepareTrack({async request(){return reply;}},f),/回执无效|绑定不一致/);
  }
});
test('rejects invalid requests before contacting device',async()=>{
  for(const override of [{operationId:bin(16,0)},{manifestUploadId:bin(16,2)},{generation:0},{revision:2},
    {parentHash:bin(32,6)},{revisionHash:bin(32,0)}]){
    await assert.rejects(prepareTrack({async request(){assert.fail('unexpected request');}}, {...fixture(),...override}),/请求或版本绑定/);
  }
});
test('supports update binding and propagates stale identity without retry',async()=>{
  const f={...fixture(),revision:2,parentHash:bin(32,6)};
  const result=await prepareTrack({async request(op,body){assert.deepEqual(body[4],f.parentHash);return ready(f);}},f);
  assert.equal(result.revision,2);
  const stale=Object.assign(Error('stale'),{code:9});
  await assert.rejects(prepareTrack({async request(){throw stale;}},f),e=>e===stale);
});
test('timeout and cancellation never report prepared',async()=>{
  const f=fixture();let clock=0,calls=0;
  await assert.rejects(prepareTrack({async request(){calls++;return [f.operationId,0,null,null,0];}},f,
    {timeoutMs:20,retryMs:10,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/尚未确认/);
  assert.equal(calls,2);
  const controller=new AbortController(),phases=[];
  await assert.rejects(prepareTrack({async request(){controller.abort();return ready(f);}},f,
    {signal:controller.signal,onProgress:p=>phases.push(p.phase)}),{name:'AbortError'});
  assert.deepEqual(phases,['checking']);
});
