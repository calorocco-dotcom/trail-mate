import test from 'node:test';import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {uploadIPTrackFile} from '../../site/tracks/src/ip-file-upload.js';
import {createIPTrackReadSession} from '../../site/tracks/src/ip-read-session.js';
test('bulk upload resumes original offset, uses bounded 4 KiB chunks and preserves the legacy default',async()=>{
 const source=new Uint8Array(8388608);for(let i=0;i<source.length;i++)source[i]=i%251;const digest=await Identity.fullHash(source),token=new Uint8Array(16).fill(1);let offset=3072,chunks=0,sealed=false;
 const request=async(op,body)=>{if(op===19&&body[0]===0)return[token,offset,0,4096];if(op===19&&body[0]===1){assert.equal(body[2],offset);assert.ok(body[3].length<=4096);assert.deepEqual(body[3],source.slice(offset,offset+body[3].length));offset+=body[3].length;chunks++;return[token,offset];}if(op===6){sealed=true;return[digest,source.length,1,0];}throw Error('Unexpected operation');};
 await uploadIPTrackFile(request,source,digest,{bulkFiles:true});assert.equal(offset,source.length);assert.equal(chunks,Math.ceil((source.length-3072)/4096));assert.equal(sealed,true);
 const ops=[];await uploadIPTrackFile(async(op,body)=>{ops.push(op);if(op===4)return[token,0,0,1024];if(op===5)return[token,body[1]+body[2].length];return[digest,source.length,1,0];},source,digest);assert.equal(ops.filter(op=>op===5).length,8192);assert.equal(ops.includes(19),false);
});
test('bulk admission mismatch and abort do not proceed to seal',async()=>{
 const source=new Uint8Array(9000).fill(7),digest=await Identity.fullHash(source),token=new Uint8Array(16).fill(2);let calls=0;
 await assert.rejects(uploadIPTrackFile(async()=>{calls++;return[token,0,0,1024];},source,digest,{bulkFiles:true}));assert.equal(calls,1);
 const abort=new AbortController(),ops=[];await assert.rejects(uploadIPTrackFile(async(op,body)=>{ops.push(op);if(body[0]===0)return[token,0,0,4096];abort.abort();return[token,body[2]+body[3].length];},source,digest,{bulkFiles:true,signal:abort.signal}),error=>error.name==='AbortError');assert.deepEqual(ops,[19,19]);
});
test('read-only IP adapter maps only file reads to bulk and rejects raw write subcommands',async()=>{
 for(const bulk of [false,true]){const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),delivery=new Uint8Array(16).fill(4),epoch=new Uint8Array(16).fill(5),entry={ready:true,epoch,destination:delivery,identity,operations:[0,1,2,8,...(bulk?[19]:[])]},calls=[],network={generation:1,directories:new Map([['key',entry]]),enqueue:work=>work(),request:async(entry,op,body)=>{calls.push({op,body});return[];}};
  const reader=await createIPTrackReadSession({network,directory:{key:'key',capabilitiesVerified:true,publicKey,delivery,epoch},assertCurrent:()=>{}});assert.equal(reader.capabilities.chunkBytes,bulk?4096:1024);const hash=new Uint8Array(32);await reader.request(8,[hash,0,bulk?4096:1024]);assert.equal(calls[0].op,bulk?19:8);assert.equal(calls[0].body.length,bulk?4:3);if(bulk)assert.equal(calls[0].body[0],2);
  assert.throws(()=>reader.request(19,[1,new Uint8Array(16),0,new Uint8Array(4096)]));assert.throws(()=>reader.request(5,[]));assert.equal(calls.length,1);reader.close();assert.throws(()=>reader.request(8,[hash,0,1]));
 }
});
