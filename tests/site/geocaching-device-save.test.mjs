import test from 'node:test';
import assert from 'node:assert/strict';
import {saveDeviceDraft} from '../../site/geocaching/src/device-draft-save.js';
import {decode} from '../../site/geocaching/src/protocol.js';
const id=new Uint8Array(16).fill(1),transferId=new Uint8Array(16).fill(3),author={verified:true,publicKey:new Uint8Array(64).fill(2),generation:7};
const fields={name:'Saved draft',description:'long description '.repeat(50)};
test('full draft uses shared kind two upload then waits for the matching durable receipt',async()=>{
  let offset=0,total=0,commits=0;const chunks=[],calls=[];
  const session={async request(op,body){calls.push(op);
    if(op===32){assert.equal(body[1],2);total=body[2];assert.equal(body[3].length,32);return [transferId,320,offset,0];}
    if(op===33){assert.equal(body[1],offset);chunks.push(body[2]);offset+=body[2].length;return [transferId,320,offset,0];}
    if(op===35)return [transferId,320,offset,1];
    assert.equal(op,66);assert.deepEqual(body,[transferId,id,0,7]);if(!commits++){const e=Error('busy');e.code=5;throw e;}return [transferId,id,1];
  }};
  const progress=[];const result=await saveDeviceDraft(session,id,fields,0,author,{transferId,sleep:async()=>{},onProgress:p=>progress.push(p)});
  assert.equal(offset,total);assert.ok(chunks.length>1);assert.equal(result.generation,1);assert.equal(progress.at(-1).phase,'saved');
  const encoded=decode(result.source,4096);assert.equal(encoded[0],null);assert.equal(encoded[1],null);assert.equal(encoded[2][4],fields.description);
  assert.deepEqual(calls.slice(-2),[66,66]);
});
test('sealed retries do not send chunks or advance the draft generation twice',async()=>{
  const calls=[];const session={async request(op,body){calls.push(op);return op===32?[transferId,320,body[2],1]:[transferId,id,5];}};
  assert.equal((await saveDeviceDraft(session,id,fields,4,author,{transferId})).generation,5);
  assert.deepEqual(calls,[32,66]);
});

test('default save budget accepts a durable reply after a slow forty-second commit',async()=>{
  let clock=0,commits=0;
  const session={async request(op,body){if(op===32)return [transferId,320,body[2],1];assert.equal(op,66);assert.deepEqual(body,[transferId,id,0,7]);if(!commits++)throw Object.assign(Error('busy'),{code:5});return [transferId,id,1];}};
  const saved=await saveDeviceDraft(session,id,fields,0,author,{transferId,now:()=>clock,sleep:async()=>{clock+=40000;}});
  assert.equal(saved.generation,1);assert.equal(commits,2);assert.equal(clock,40000);
});
test('wrong receipt and changed identity cannot report saved contents',async()=>{
  const session={async request(op,body){if(op===32)return [transferId,320,body[2],1];return [transferId,new Uint8Array(16),1];}};
  await assert.rejects(saveDeviceDraft(session,id,fields,0,author,{transferId}),/回执无效/);
  const stale={async request(op,body){if(op===32)return [transferId,320,body[2],1];const e=Error('identity changed');e.code=9;throw e;}};
  await assert.rejects(saveDeviceDraft(stale,id,fields,0,author,{transferId}),/identity changed/);
});
test('bounded Busy and cancellation after a commit reply leave the result unconfirmed',async()=>{
  let clock=0;const busy={async request(op,body){if(op===32)return [transferId,320,body[2],1];const e=Error('busy');e.code=5;throw e;}};
  await assert.rejects(saveDeviceDraft(busy,id,fields,0,author,{transferId,timeoutMs:200,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/未确认/);
  const controller=new AbortController();const cancelled={async request(op,body){if(op===32)return [transferId,320,body[2],1];controller.abort();return [transferId,id,1];}};
  await assert.rejects(saveDeviceDraft(cancelled,id,fields,0,author,{transferId,signal:controller.signal}),{name:'AbortError'});
});
