import test from 'node:test';
import assert from 'node:assert/strict';
import {commitDeviceDraft} from '../../site/geocaching/src/device-draft-commit.js';
import {DeviceDraftReader} from '../../site/geocaching/src/device-draft-reader.js';
const fixture=()=>{
  const publicKey=new Uint8Array(64).fill(2);
  return {author:{verified:true,publicKey,generation:7},prepared:{prepareId:new Uint8Array(16).fill(1),id:new Uint8Array(16).fill(3),
    generation:1,authorKey:publicKey.slice(),authorGeneration:7,directory:new Uint8Array(16).fill(4),directoryEpoch:new Uint8Array(16).fill(5),
    contentHash:new Uint8Array(32).fill(6),fromRevision:0,toRevision:1,expiresAt:120000}};
};
const reply=f=>[f.prepared.prepareId.slice(),f.prepared.id.slice(),f.prepared.generation,f.prepared.directory.slice(),f.prepared.contentHash.slice(),f.prepared.toRevision,1];

test('commits the retained prepared ID across Busy, reporting local admission rather than publication',async()=>{
  const f=fixture(),calls=[];let clock=0;
  const session={async request(op,body){calls.push([op,body]);if(calls.length===1)throw Object.assign(Error('busy'),{code:5});return reply(f);}};
  const result=await commitDeviceDraft(session,f.prepared,f.author,{now:()=>clock,sleep:async ms=>{clock+=ms;}});
  assert.equal(result.localAdmitted,true);assert.equal(result.directoryConfirmed,false);
  assert.deepEqual(calls,[[68,[f.prepared.prepareId,f.prepared.contentHash,7]],[68,[f.prepared.prepareId,f.prepared.contentHash,7]]]);
});

test('rejects foreign identity, invalid bindings and initial expiry without requests',async()=>{
  const f=fixture();let calls=0;const session={async request(){calls++;}};
  await assert.rejects(commitDeviceDraft(session,f.prepared,{...f.author,verified:false}));
  await assert.rejects(commitDeviceDraft(session,{...f.prepared,authorGeneration:8},f.author));
  await assert.rejects(commitDeviceDraft(session,{...f.prepared,toRevision:3},f.author));
  await assert.rejects(commitDeviceDraft(session,f.prepared,f.author,{now:()=>120000}),/已过期/);
  assert.equal(calls,0);
});

test('a genuine admission remains valid after the prepared lifetime passes in flight',async()=>{
  const f=fixture();let now=119999;
  const result=await commitDeviceDraft({async request(){now=120001;return reply(f);}},f.prepared,f.author,{now:()=>now});
  assert.equal(result.localAdmitted,true);assert.equal(result.directoryConfirmed,false);
});

test('explicit resumption retains an expired operation ID and relies on the device admission evidence',async()=>{
  const f=fixture(),calls=[];
  const result=await commitDeviceDraft({async request(op,body){calls.push([op,body]);return reply(f);}},f.prepared,f.author,
    {resume:true,now:()=>500000});
  assert.equal(result.localAdmitted,true);assert.equal(result.directoryConfirmed,false);
  assert.deepEqual(calls,[[68,[f.prepared.prepareId,f.prepared.contentHash,7]]]);
});

test('cancelled confirmation or expiry in a progress callback sends no commit request',async()=>{
  const f=fixture();let calls=0,clock=0;
  const session={async request(){calls++;}},controller=new AbortController();
  await assert.rejects(commitDeviceDraft(session,f.prepared,f.author,{now:()=>0,signal:controller.signal,
    onProgress:()=>controller.abort()}),{name:'AbortError'});
  await assert.rejects(commitDeviceDraft(session,f.prepared,f.author,{now:()=>clock,timeoutMs:200000,
    onProgress:()=>{clock=120000;}}),/已过期/);
  assert.equal(calls,0);
});

test('refuses every mismatched admission field and never starts a replacement operation',async()=>{
  const f=fixture();
  for(const mutate of [r=>r.pop(),r=>r[0].fill(9),r=>r[1].fill(9),r=>r[2]=2,r=>r[3].fill(9),r=>r[4].fill(9),r=>r[5]=2,r=>r[6]=true]){
    const receipt=reply(f);mutate(receipt);const calls=[];
    await assert.rejects(commitDeviceDraft({async request(op){calls.push(op);return receipt;}},f.prepared,f.author,{now:()=>0}));
    assert.deepEqual(calls,[68]);
  }
});

test('lost responses and a bounded Busy deadline retain the original operation',async()=>{
  const f=fixture();
  await assert.rejects(commitDeviceDraft({async request(){throw Object.assign(Error('USB timeout'),{code:'TIMEOUT'});}},f.prepared,f.author,{now:()=>0}),
    e=>e.code==='TIMEOUT'&&e.operationId[0]===1);
  let clock=0;
  await assert.rejects(commitDeviceDraft({async request(){throw Object.assign(Error('busy'),{code:5});}},f.prepared,f.author,
    {now:()=>clock,timeoutMs:2,retryMs:1,sleep:async ms=>{clock+=ms;}}),e=>e.code==='COMMIT_PENDING'&&e.operationId[0]===1);
});

test('the reader freezes author and prepared values and closes only the shared read lease',async()=>{
  const f=fixture(),receipt=reply(f),calls=[];
  const session={async request(op,body){calls.push([op,body]);return op===3?[]:receipt;}};
  const reader=new DeviceDraftReader(session),work=reader.commit(f.prepared,f.author,{now:()=>0});
  f.prepared.prepareId.fill(9);f.prepared.contentHash.fill(9);f.prepared.authorKey.fill(9);f.author.generation=8;
  const result=await work;
  assert.equal(result.operationId[0],1);assert.equal(result.contentHash[0],6);
  assert.deepEqual(calls,[[68,[new Uint8Array(16).fill(1),new Uint8Array(32).fill(6),7]],[3,[]]]);await reader.close();
});
