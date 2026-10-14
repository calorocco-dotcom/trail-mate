import test from 'node:test';
import assert from 'node:assert/strict';
import {encode} from '../../site/geocaching/src/protocol.js';
import {readDeviceDraft} from '../../site/geocaching/src/device-maintenance.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';
const id=new Uint8Array(16).fill(1),author={verified:true,publicKey:new Uint8Array(64).fill(2),generation:7};
const data=encode([author.publicKey,null,[0,100,200,'Own draft','description '.repeat(70),'hint',2,3,1],4]);
function reader(transform=reply=>reply){return {async request(op,[key,offset,count,generation,authorGeneration]){
  assert.equal(op,OPERATION.CACHE_DRAFT_GET);assert.deepEqual(key,id);assert.equal(authorGeneration,7);assert.equal(generation,offset?4:0);
  return transform([id,4,data.length,offset,data.slice(offset,offset+count)]);
}};}
test('device draft reads bind author and draft generations across chunks',async()=>{
  let attempts=0;const source=reader(),session={async request(...args){if(!attempts++){const error=new Error('busy');error.code=5;throw error;}return source.request(...args);}};
  const progress=[];const result=await readDeviceDraft(session,id,author,{sleep:async()=>{},onProgress:p=>progress.push(p)});
  assert.deepEqual(result.bytes,data);assert.equal(result.draft.generation,4);assert.equal(result.editable,true);assert.ok(progress.length>=3);
});
test('changed generation, wrong ID and stalled chunks cannot replace a draft',async()=>{
  await assert.rejects(readDeviceDraft(reader(reply=>reply[3]?[reply[0],5,...reply.slice(2)]:reply),id,author),/发生变化/);
  await assert.rejects(readDeviceDraft(reader(reply=>[new Uint8Array(16),...reply.slice(1)]),id,author),/响应无效/);
  await assert.rejects(readDeviceDraft(reader(reply=>[...reply.slice(0,4),new Uint8Array()]),id,author),/响应无效/);
});
test('identity changes fail immediately and Busy polling has a finite deadline',async()=>{
  const stale={async request(){const error=new Error('identity changed');error.code=9;throw error;}};
  await assert.rejects(readDeviceDraft(stale,id,author),/identity changed/);
  let clock=0;const busy={async request(){const error=new Error('busy');error.code=5;throw error;}};
  await assert.rejects(readDeviceDraft(busy,id,author,{timeoutMs:250,retryMs:100,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/超时/);
});
test('unverified devices are rejected and old-author drafts are read-only',async()=>{
  await assert.rejects(readDeviceDraft(reader(),id,{...author,verified:false}),/必须验证/);
  const legacy=encode([new Uint8Array(64).fill(3),null,[0,null,null,'Legacy','','',2,2,0],4]);
  const session={async request(){return [id,4,legacy.length,0,legacy];}};
  assert.equal((await readDeviceDraft(session,id,author)).editable,false);
});

test('cancellation and deadline after the final reply never return editor contents',async()=>{
  const small=encode([null,null,[0,null,null,'Draft','','',2,2,0],4]);
  const controller=new AbortController();
  const cancelled={async request(){controller.abort(new Error('cancelled during reply'));return [id,4,small.length,0,small];}};
  await assert.rejects(readDeviceDraft(cancelled,id,author,{signal:controller.signal}),/cancelled during reply/);
  let clock=0;
  const expired={async request(){clock=100;return [id,4,small.length,0,small];}};
  await assert.rejects(readDeviceDraft(expired,id,author,{timeoutMs:100,now:()=>clock}),/超时/);
});

test('caller identity mutation cannot change continuation binding or authorship',async()=>{
  const mutableAuthor={...author,publicKey:author.publicKey.slice()},mutableId=id.slice();
  const source=reader();let first=true;
  const session={async request(...args){
    const reply=await source.request(...args);
    if(first){first=false;mutableAuthor.generation=8;mutableAuthor.publicKey.fill(3);mutableId.fill(9);}
    return reply;
  }};
  const result=await readDeviceDraft(session,mutableId,mutableAuthor);
  assert.equal(result.editable,true);assert.deepEqual(result.bytes,data);
});
