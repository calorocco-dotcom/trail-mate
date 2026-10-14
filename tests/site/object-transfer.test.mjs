import test from 'node:test';
import assert from 'node:assert/strict';
import {uploadObject} from '../../site/device/src/object-transfer.js';
import {OPERATION,encodeManagement} from '../../site/device/src/management-protocol.js';
const id=new Uint8Array(16).fill(3),sha256=new Uint8Array(32).fill(4);

test('admission Busy retries one frozen binding but transport loss never resubmits',async()=>{
 const source=new Uint8Array([1,2,3]),original=source.slice(),selected=id.slice(),digest=sha256.slice(),calls=[];let begins=0;
 const session={async request(op,body){calls.push(op);assert.equal(op,OPERATION.TRANSFER_BEGIN);assert.deepEqual(body,[id,5,original.length,sha256]);if(!begins++){selected.fill(0);digest.fill(0);source.fill(9);throw Object.assign(Error('busy'),{code:5});}return [id,320,original.length,1];}};
 const done=await uploadObject(session,source,{id:selected,kind:5,sha256:digest,beginPollMs:0});assert.deepEqual(done.id,id);assert.equal(begins,2);assert.deepEqual(calls,[32,32]);
 let requests=0;await assert.rejects(uploadObject({async request(){requests++;throw Object.assign(Error('lost reply'),{code:'TIMEOUT'});}},original,{id,kind:5,sha256,beginPollMs:0}),/lost reply/);assert.equal(requests,1);
});

test('admission timeout and cancellation retain the binding without chunks or deletion',async()=>{
 const calls=[],source=new Uint8Array(3);
 await assert.rejects(uploadObject({async request(op){calls.push(op);throw Object.assign(Error('busy'),{code:5});}},source,{id,kind:5,sha256,beginTimeoutMs:2,beginPollMs:1}),/admission timed out/);assert.ok(calls.length>=1&&calls.every(op=>op===32));
 const controller=new AbortController(),cancelled=[];
 await assert.rejects(uploadObject({async request(op){cancelled.push(op);controller.abort();throw Object.assign(Error('busy'),{code:5});}},source,{id,kind:5,sha256,signal:controller.signal}),error=>error.name==='AbortError');assert.deepEqual(cancelled,[32]);
});
test('upload resumes at durable offset and negotiates every chunk without overlap',async()=>{
 const source=Uint8Array.from({length:900},(_,i)=>i%256),chunks=[];let offset=100;
 const session={async request(op,body){
   assert.ok(encodeManagement(id,id,op,body).length<=512);
   if(op===OPERATION.TRANSFER_CHUNK){assert.equal(body[1],offset);assert.deepEqual(body[2],source.slice(offset,offset+body[2].length));chunks.push(body[2].length);offset+=body[2].length;}
   return [id,384,offset,op===OPERATION.TRANSFER_FINISH?1:0];
 }};
 const progress=[];await uploadObject(session,source,{id,kind:1,sha256,onProgress:p=>progress.push(p)});
 assert.deepEqual(chunks,[384,384,32]);assert.equal(progress.at(-1).sealed,true);
});
test('fully sealed object is idempotent and unexpected offsets fail',async()=>{
 let calls=0;const source=new Uint8Array(10);
 await uploadObject({async request(){calls++;return [id,384,10,1]}},source,{id,kind:1,sha256});assert.equal(calls,1);
 let offset=0;await assert.rejects(uploadObject({async request(op){if(op===OPERATION.TRANSFER_CHUNK)offset=5;return [id,384,offset,0]}},source,{id,kind:1,sha256}),/acknowledgement/);
});
test('pause does not implicitly delete partially transferred device object',async()=>{
 const controller=new AbortController();controller.abort();let calls=0;
 await assert.rejects(uploadObject({async request(){calls++}},new Uint8Array(10),{id,kind:1,sha256,signal:controller.signal}));assert.equal(calls,0);
});
test('asynchronous finish retries Busy and unsealed status without reporting completion',async()=>{
 const source=new Uint8Array(10),progress=[];let offset=0,finishes=0;
 const session={async request(op){if(op===OPERATION.TRANSFER_CHUNK)offset=10;if(op===OPERATION.TRANSFER_FINISH){if(++finishes===1)throw Object.assign(Error('busy'),{code:5});return [id,384,offset,finishes>=3?1:0];}return [id,384,offset,0];}};
 await uploadObject(session,source,{id,kind:1,sha256,finishPollMs:0,onProgress:value=>progress.push(value)});assert.equal(finishes,3);assert.equal(progress.filter(value=>value.sealed).length,1);assert.equal(progress.at(-2).stage,'validating');
});
test('input changes cannot replace frozen ID, hash or source',async()=>{
 const selected=id.slice(),digest=sha256.slice(),source=new Uint8Array([1,2,3]);let offset=0;
 const session={async request(op,body){assert.deepEqual(body[0],id);if(op===OPERATION.TRANSFER_BEGIN){selected.fill(0);digest.fill(0);source.fill(0);}if(op===OPERATION.TRANSFER_CHUNK){assert.deepEqual(body[2],new Uint8Array([1,2,3]));offset=3;}return [id,384,offset,op===OPERATION.TRANSFER_FINISH?1:0];}};
 const receipt=await uploadObject(session,source,{id:selected,kind:1,sha256:digest});assert.deepEqual(receipt.id,id);assert.deepEqual(receipt.sha256,sha256);
});
test('late cancellation rejects seal and timeout retains transfer',async()=>{
 const abort=new AbortController(),calls=[];let offset=0;
 await assert.rejects(uploadObject({async request(op){calls.push(op);if(op===OPERATION.TRANSFER_CHUNK)offset=10;if(op===OPERATION.TRANSFER_FINISH)abort.abort();return [id,384,offset,op===OPERATION.TRANSFER_FINISH?1:0];}},new Uint8Array(10),{id,kind:1,sha256,signal:abort.signal}),error=>error.name==='AbortError');assert.ok(!calls.includes(OPERATION.TRANSFER_ABORT));
 offset=0;await assert.rejects(uploadObject({async request(op){if(op===OPERATION.TRANSFER_CHUNK)offset=10;return [id,384,offset,0];}},new Uint8Array(10),{id,kind:1,sha256,finishTimeoutMs:2,finishPollMs:1}),/timed out/);
});
