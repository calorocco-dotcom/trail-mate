import test from 'node:test';import assert from 'node:assert/strict';import {webcrypto} from 'node:crypto';
import {uploadTrackFile,uploadTrackManifest,queryTrackUpload,discardTrackUpload} from '../../site/tracks/src/device-upload.js';
import {encodeTrackManifest} from '../../site/tracks/src/manifest.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';
const id=new Uint8Array(16).fill(1),digest=async value=>new Uint8Array(await webcrypto.subtle.digest('SHA-256',value));
const manifestBytes=()=>encodeTrackManifest([1,new Uint8Array(64).fill(2),new Uint8Array(16).fill(3),0,1,null,0,1,1,'test','',[0,0,0,0],[0,null,null,1],[new Uint8Array(32).fill(4),10,'application/vnd.google-earth.kml+xml',1]]);
test('manifest uploads validated frozen bytes with kind4 and shares file concurrency guard',async()=>{
 const source=manifestBytes(),original=source.slice(),received=[];let offset=0,release;
 const session={async request(op,body){if(op===OPERATION.TRANSFER_BEGIN){assert.equal(body[1],4);assert.equal(body[2],original.length);assert.deepEqual(body[3],await digest(original));}else if(op===OPERATION.TRANSFER_CHUNK){received.push(...body[2]);offset+=body[2].length;}return [body[0],320,offset,op===OPERATION.TRANSFER_FINISH?1:0];}};
 const pending=uploadTrackManifest(session,source,{id,digest:bytes=>new Promise(resolve=>{release=async()=>resolve(await digest(bytes));})});
 source.fill(0);
 await assert.rejects(uploadTrackFile(session,new Uint8Array([1]),{id,digest}),/已有/);
 await assert.rejects(discardTrackUpload(session,id),/暂停/);
 await release();const result=await pending;
 assert.deepEqual(new Uint8Array(received),original);assert.equal(result.sealed,true);assert.equal(result.published,false);
});
test('invalid or oversized manifests never send USB requests',async()=>{
 let calls=0;const session={async request(){calls++;}};
 for(const source of [new Uint8Array(4097),new Uint8Array([0x90]),new Uint8Array([0xc0])])await assert.rejects(uploadTrackManifest(session,source,{id,digest}));
 assert.equal(calls,0);
});
test('track upload preserves bytes and only returns sealed staging after finish polling',async()=>{
 const source=new Uint8Array(750).fill(7),original=source.slice(),received=[],progress=[];let offset=0,total=0,polls=0;
 const session={async request(op,body){if(op===OPERATION.TRANSFER_BEGIN){assert.equal(body[1],3);total=body[2];assert.deepEqual(body[3],await digest(original));}else if(op===OPERATION.TRANSFER_CHUNK){assert.equal(body[1],offset);assert.ok(body[2].length<=320);received.push(...body[2]);offset+=body[2].length;}else if(op===OPERATION.TRANSFER_FINISH)polls++;return [body[0],320,offset,polls>=2?1:0];}};
 const result=await uploadTrackFile(session,source,{id,digest:async bytes=>{source.fill(99);return digest(bytes);},finishPollMs:0,onProgress:value=>progress.push(value)});
 assert.deepEqual(new Uint8Array(received),original);assert.equal(result.bytes,total);assert.equal(result.sealed,true);assert.equal(result.published,false);assert.equal(polls,2);assert.ok(progress.some(value=>value.stage==='validating'));
});
test('status and explicit discard validate device receipts',async()=>{
 const session={async request(op,body){assert.deepEqual(body,[id]);return op===OPERATION.TRANSFER_STATUS?[id,320,500,0]:[];}};
 assert.equal((await queryTrackUpload(session,id,750)).offset,500);assert.equal(await discardTrackUpload(session,id),true);
 await assert.rejects(queryTrackUpload({async request(){return [id,320,500,1];}},id,750),/状态/);
 await assert.rejects(discardTrackUpload({async request(){return [1];}},id),/回执/);
 await assert.rejects(uploadTrackFile(session,new Uint8Array(1),{id:new Uint8Array(16),digest}),/ID/);
});
test('cancel during hashing sends nothing and releases the session for retry',async()=>{
 let release,calls=0;const controller=new AbortController();
 const session={async request(op,body){calls++;return op===OPERATION.TRANSFER_ABORT?[]:[body[0],320,1,1];}};
 const pending=uploadTrackFile(session,new Uint8Array([7]),{id,signal:controller.signal,digest:()=>new Promise(resolve=>{release=resolve;})});
 await assert.rejects(uploadTrackFile(session,new Uint8Array([7]),{id,digest}),/已有/);
 await assert.rejects(discardTrackUpload(session,id),/暂停/);
 controller.abort(Error('paused'));release(new Uint8Array(32));
 await assert.rejects(pending,/paused/);assert.equal(calls,0);
 assert.equal((await uploadTrackFile(session,new Uint8Array([7]),{id,digest})).sealed,true);
 assert.equal(await discardTrackUpload(session,id),true);
});
test('hash failures never reach USB and do not retain the active marker',async()=>{
 let calls=0;const session={async request(op,body){calls++;return [body[0],320,1,1];}};
 for(const bad of [async()=>{throw Error('hash failed');},async()=>new Uint8Array(31)]){
  await assert.rejects(uploadTrackFile(session,new Uint8Array([1]),{id,digest:bad}));assert.equal(calls,0);
 }
 assert.equal((await uploadTrackFile(session,new Uint8Array([1]),{id,digest})).bytes,1);
});
test('track upload rejects oversized negotiated budgets before sending file blocks',async()=>{
 const calls=[];const session={async request(op,body){calls.push(op);return [body[0],384,0,0];}};
 await assert.rejects(uploadTrackFile(session,new Uint8Array(640),{id,digest}),/320/);
 assert.deepEqual(calls,[OPERATION.TRANSFER_BEGIN]);
 assert.ok(!calls.includes(OPERATION.TRANSFER_ABORT));
});
