import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {createDeviceTrackReadSession} from '../../site/tracks/src/device-shared-read.js';
import {createTrackRegionClient} from '../../site/tracks/src/region-client.js';
import {OPERATION as OP} from '../../site/device/src/management-protocol.js';
test('region protocol client traverses USB management adapter with verified metadata and persistent context',async()=>{
 const peer=await Identity.generate(),publicKey=await peer.getPublicKey(),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),binding=new Uint8Array(26);binding.set(prefix);binding.set(peer.identityHash,10);
 const directory={delivery:await Identity.truncatedHash(binding),publicKey,epoch:new Uint8Array(16).fill(4),capabilitiesVerified:true,fileLimit:8388608,chunkLimit:1024,queryLimit:32};
 const author={verified:true,publicKey,generation:9},operations=[91,92,93,94],version=new Uint8Array(32).fill(1);
 const root=['test:r',null,0,'Root',[],[0,0,10000000,10000000],1],child=['test:c','test:r',1,'Child',[],[0,0,10000000,10000000],0];
 const boundary=new TextEncoder().encode(JSON.stringify([1,Buffer.from(version).toString('hex'),'test:r',[[[[0,0],[10000000,0],[10000000,10000000],[0,10000000],[0,0]]]]])),digest=await Identity.fullHash(boundary);
 let current;const calls=[];
 const session={async request(operation,body){
  if(operation===OP.TRACK_READ_BEGIN){
   const [id,delivery,epoch,generation,op,params]=body;assert.deepEqual(delivery,directory.delivery);assert.deepEqual(epoch,directory.epoch);assert.equal(generation,9);calls.push([op,params]);let result;
   if(op===9){assert.deepEqual(params,[]);result=[1,'trailmate.track.regions.v1','test',version,1,32,37,[9,10,11,12,13]];}
   if(op===10){assert.equal(params[0],'test');assert.deepEqual(params[1],version);assert.equal(params[3],params[2]===null?-1:0);assert.equal(params[5],1);result=[version,null,[params[2]===null?root:child]];}
   if(op===11){assert.deepEqual(params,['test','test:r',version,1]);result=[version,root,['region-e7-json-v1',digest,boundary.length]];}
   if(op===12){assert.deepEqual(params.slice(0,5),['test','test:r',version,digest,boundary.length]);result=[digest,boundary.length,params[5],boundary.slice(params[5],params[5]+params[6])];}
   if(op===13){assert.deepEqual(params.slice(0,4),['test','test:r',version,root[5]]);result=[9007199254740993n,null,[]];}
   const raw=encode([1,1,op,id,200,result]);current={id,op,raw,hash:await Identity.fullHash(raw)};return [id,op];
  }
  if(operation===OP.TRACK_READ_STATUS)return [current.id,current.op,200,current.raw.length,current.hash];
  if(operation===OP.TRACK_READ_CHUNK)return [current.hash,current.raw.length,body[1],current.raw.slice(body[1],body[1]+body[2])];
  if(operation===OP.TRACK_READ_RELEASE){current=undefined;return [];}
  assert.fail(`Unexpected ${operation}`);
 }};
 const options={session,directory,author,operations,assertCurrent:()=>{}};
 const adapter=await createDeviceTrackReadSession(options),regions=await createTrackRegionClient(adapter.request);
 const roots=await regions.children(),selected=await regions.region(roots.items[0].id);assert.equal(selected.boundaryVerified,true);
 assert.equal((await regions.query(selected)).snapshot,9007199254740993n);
 const context=adapter.regionContext;adapter.close();
 const resumed=await createDeviceTrackReadSession({...options,regionContext:context}),resumedClient=await createTrackRegionClient(resumed.request);
 assert.equal((await resumedClient.children(roots.items[0])).items[0].id,'test:c');
 await assert.rejects(createDeviceTrackReadSession({...options,regionContext:context,directory:{...directory,epoch:new Uint8Array(16).fill(5)}}),e=>e.code==='TRACK_DIRECTORY_CONTEXT_MISMATCH');
 assert.ok(calls.some(([op])=>op===12));resumed.close();
});
