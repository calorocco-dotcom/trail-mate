import test from 'node:test';
import assert from 'node:assert/strict';
import {decodePublicationJob,readPublicationJob,stopPublicationJob,PUBLICATION_PHASE as P} from '../../site/geocaching/src/device-publication-job.js';
import {DeviceDraftReader} from '../../site/geocaching/src/device-draft-reader.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';

const id=new Uint8Array(16).fill(1),cache=new Uint8Array(32).fill(2),hash=new Uint8Array(32).fill(3);
const row=(phase,durable,transported)=>[id.slice(),phase,cache.slice(),hash.slice(),4,durable,transported,null];
const bind={operationId:id,cacheId:cache,revisionHash:hash,revision:4};

test('only the directory-confirmed phase declares publication success',()=>{
  const states=[[P.PREPARING,0,0],[P.PERSISTING,0,0],[P.WAITING,1,null],
    [P.WAITING,1,1],[P.CONFIRMED,1,1],[P.STOPPED,1,null],[P.STOPPED,1,1],
    [P.FAILED,null,null],[P.STOPPING,null,1]];
  for(const [phase,durable,transported] of states){
    const result=decodePublicationJob(decode(encode(row(phase,durable,transported))),bind);
    assert.equal(result.directoryConfirmed,phase===P.CONFIRMED);
    assert.equal(result.terminal,[P.CONFIRMED,P.STOPPED,P.FAILED].includes(phase));
    assert.equal(result.durableConfirmed,durable===null?null:durable===1);
    assert.equal(result.transportConfirmed,transported===null?null:transported===1);
  }
});

test('pre-admission and failed admission can retain null signed-object identifiers',()=>{
  for(const phase of [P.PREPARING,P.FAILED]){
    const reply=[id.slice(),phase,null,null,4,phase===P.FAILED?null:0,null,phase===P.FAILED?'Publication could not be admitted':null];
    const result=decodePublicationJob(reply,bind);
    assert.equal(result.cacheId,null);assert.equal(result.revisionHash,null);assert.equal(result.directoryConfirmed,false);
  }
});

test('rejects mismatched retained operation, signed object and revision',()=>{
  for(const mutate of [r=>r[0].fill(9),r=>r[2].fill(9),r=>r[3].fill(9),r=>r[4]=5]){
    const reply=row(P.CONFIRMED,1,1);mutate(reply);assert.throws(()=>decodePublicationJob(reply,bind));
  }
});

test('rejects unsupported phases, malformed evidence and impossible success',()=>{
  const changes=[r=>r.pop(),r=>r[1]=7,r=>r[1]=-1,r=>r[2]=null,r=>r[3]=null,
    r=>r[4]=0,r=>r[4]=0x100000000,r=>r[5]=true,r=>r[6]=false,r=>r[5]=null,
    r=>r[6]=null,r=>r[5]=0,r=>r[7]='directory rejected',r=>r[7]={message:'bad'},
    r=>r[7]='汉'.repeat(43),r=>r[7]=''];
  for(const mutate of changes){const reply=row(P.CONFIRMED,1,1);mutate(reply);
    assert.throws(()=>decodePublicationJob(reply,bind));}
  assert.throws(()=>decodePublicationJob(row(P.WAITING,0,null),bind));
  assert.throws(()=>decodePublicationJob([id,P.STOPPED,null,null,4,1,null,null],bind));
});

test('unknown failure and stopping evidence is preserved rather than changed to false',()=>{
  for(const phase of [P.FAILED,P.STOPPING]){
    const result=decodePublicationJob(row(phase,null,null),bind);
    assert.equal(result.durableConfirmed,null);assert.equal(result.transportConfirmed,null);
    assert.equal(result.directoryConfirmed,false);
  }
});

test('copies received identifiers so a reused serial arena cannot change retained evidence',()=>{
  const reply=row(P.CONFIRMED,1,1),result=decodePublicationJob(reply,bind);
  reply[0].fill(9);reply[2].fill(9);reply[3].fill(9);
  assert.deepEqual(result.operationId,id);assert.deepEqual(result.cacheId,cache);assert.deepEqual(result.revisionHash,hash);
});

test('validates the retained binding before interpreting any row',()=>{
  for(const binding of [{},{operationId:new Uint8Array(16)},{...bind,cacheId:new Uint8Array(16)},
    {...bind,revisionHash:'hash'},{...bind,revision:0},{...bind,revision:4n}])
    assert.throws(()=>decodePublicationJob(row(P.CONFIRMED,1,1),binding),/绑定/);
});

const author={verified:true,publicKey:new Uint8Array(64).fill(4),generation:7};
test('stop retries only the retained operation and preserves unknown transport evidence',async()=>{
  const calls=[];
  const session={async request(op,body){calls.push([op,body]);if(calls.length===1)throw Object.assign(Error('busy'),{code:5});return row(P.STOPPED,1,null);}};
  const result=await stopPublicationJob(session,bind,author,{sleep:async()=>{}});
  assert.equal(result.phase,P.STOPPED);assert.equal(result.transportConfirmed,null);assert.equal(result.directoryConfirmed,false);
  assert.deepEqual(calls,[[50,[id,7]],[50,[id,7]]]);
});
test('a late stop preserves the directory-confirmed result',async()=>{
  const result=await stopPublicationJob({async request(op){assert.equal(op,50);return row(P.CONFIRMED,1,1);}},bind,author);
  assert.equal(result.directoryConfirmed,true);assert.equal(result.phase,P.CONFIRMED);
});
test('a stop response cannot claim completion from Waiting or a mismatched operation',async()=>{
  await assert.rejects(stopPublicationJob({async request(){return row(P.WAITING,1,null);}},bind,author),/尚未确认停止/);
  await assert.rejects(stopPublicationJob({async request(){const result=row(P.STOPPED,1,null);result[0].fill(9);return result;}},bind,author),/不一致/);
});
test('unknown stop evidence retains the ID and always releases the shared reader',async()=>{
  const calls=[];
  const session={async request(op){calls.push(op);if(op===3)return [];throw Object.assign(Error('missing'),{code:10});}};
  const reader=new DeviceDraftReader(session);
  await assert.rejects(reader.stop(bind,author),error=>error.code==='JOB_UNKNOWN'&&error.operationId[0]===1);
  assert.deepEqual(calls,[50,3]);await reader.close();
});
test('queries the same operation across Busy and returns Waiting without republishing',async()=>{
  const calls=[];const session={async request(op,body){calls.push([op,body]);if(calls.length===1)throw Object.assign(Error('busy'),{code:5});
    return row(P.WAITING,1,null);}};
  const result=await readPublicationJob(session,bind,author,{sleep:async()=>{}});
  assert.equal(result.phase,P.WAITING);assert.equal(result.directoryConfirmed,false);
  assert.deepEqual(calls,[[48,[id,7]],[48,[id,7]]]);
});

test('missing retained history preserves the operation and issues no prepare or commit',async()=>{
  const calls=[];const session={async request(op){calls.push(op);throw Object.assign(Error('missing'),{code:10});}};
  await assert.rejects(readPublicationJob(session,bind,author),error=>error.code==='JOB_UNKNOWN'&&error.operationId[0]===1);
  assert.deepEqual(calls,[48]);
});

test('bounds Busy retries and refuses a late reply after cancellation',async()=>{
  let now=0;const session={async request(){throw Object.assign(Error('busy'),{code:5});}};
  await assert.rejects(readPublicationJob(session,bind,author,{now:()=>now,timeoutMs:2,retryMs:1,sleep:async ms=>{now+=ms;}}),
    error=>error.code==='JOB_PENDING'&&error.operationId[0]===1);
  const controller=new AbortController();
  await assert.rejects(readPublicationJob({async request(){controller.abort();return row(P.CONFIRMED,1,1);}},bind,author,
    {signal:controller.signal}),{name:'AbortError'});
});

test('unverified authors, invalid operation bindings and budgets never query the device',async()=>{
  let calls=0;const session={async request(){calls++;throw Error('unexpected');}};
  await assert.rejects(readPublicationJob(session,bind,{...author,verified:false}));
  await assert.rejects(readPublicationJob(session,{...bind,operationId:new Uint8Array(16)},author));
  await assert.rejects(readPublicationJob(session,{...bind,revision:0},author));
  await assert.rejects(readPublicationJob(session,bind,author,{timeoutMs:0}));
  assert.equal(calls,0);
});

test('reader freezes the query binding and serializes SESSION_CLOSE after the job snapshot',async()=>{
  const binding={operationId:id.slice(),cacheId:cache.slice(),revisionHash:hash.slice(),revision:4};
  const identity={...author,publicKey:author.publicKey.slice()},calls=[];
  const session={async request(op,body){calls.push([op,body]);return op===3?[]:row(P.CONFIRMED,1,1);}};
  const reader=new DeviceDraftReader(session),work=reader.job(binding,identity);
  binding.operationId.fill(9);binding.cacheId.fill(9);binding.revisionHash.fill(9);binding.revision=5;identity.generation=8;
  assert.equal((await work).directoryConfirmed,true);
  assert.deepEqual(calls,[[48,[id,7]],[3,[]]]);await reader.close();
});

test('unknown and invalid snapshots still close the shared reader without cancelling the publication',async()=>{
  for(const unknown of [true,false]){
    const calls=[];const session={async request(op){calls.push(op);if(op===3)return [];
      if(unknown)throw Object.assign(Error('missing'),{code:10});
      const reply=row(P.CONFIRMED,1,1);reply[0].fill(9);return reply;}};
    const reader=new DeviceDraftReader(session);
    await assert.rejects(reader.job(bind,author));assert.deepEqual(calls,[48,3]);await reader.close();
  }
});
