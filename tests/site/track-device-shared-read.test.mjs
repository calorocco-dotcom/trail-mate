import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {createDeviceTrackReadSession} from '../../site/tracks/src/device-shared-read.js';
import {OPERATION as OP,MANAGEMENT_STATUS as STATUS} from '../../site/device/src/management-protocol.js';
import {readSharedTrack} from '../../site/tracks/src/shared-track-read.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {trackId} from '../../site/tracks/src/shared-id.js';
const dirIdentity=await Identity.generate(),key=await dirIdentity.getPublicKey();
const prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),binding=new Uint8Array(26);
binding.set(prefix);binding.set(dirIdentity.identityHash,10);
const directory={delivery:await Identity.truncatedHash(binding),publicKey:key,epoch:new Uint8Array(16).fill(2),capabilitiesVerified:true,fileLimit:8388608,chunkLimit:1024,queryLimit:32};
const signer=await Identity.generate(),author={publicKey:await signer.getPublicKey(),generation:9,verified:true};
const source=new TextEncoder().encode('<kml><Placemark><LineString><coordinates>120,30 121,31</coordinates></LineString></Placemark></kml>');
const fileHash=await Identity.fullHash(source);
const record=[1,author.publicKey,new Uint8Array(16).fill(3),0,1,null,0,100,100,'USB shared trail','',[1200000000,300000000,1210000000,310000000],[0,null,null,2],[fileHash,source.length,'application/vnd.google-earth.kml+xml',1]];
const rawManifest=encodeTrackManifest(record),signed=[rawManifest,await signer.sign(trackManifestSigningBytes(rawManifest))],manifest=await verifyTrackManifest(signed);
const operations=[OP.TRACK_READ_BEGIN,OP.TRACK_READ_STATUS,OP.TRACK_READ_CHUNK,OP.TRACK_READ_RELEASE];
function fixture({corrupt=false,busyRelease=false,lostAck=false,remote=false}={}){
 let current,releaseAttempts=0;const calls=[];
 const session={async request(op,body){calls.push([op,body]);
  if(op===OP.TRACK_READ_BEGIN){
   const [id,delivery,epoch,generation,read,params]=body;
   assert.deepEqual(delivery,directory.delivery);assert.deepEqual(epoch,directory.epoch);assert.equal(generation,9);
   let result;if(read===2)result=[signed,1];else if(read===8){assert.equal(params[1],source.length);result=[fileHash,source.length,params[2],source.slice(params[2],params[2]+params[3])];}else result=[[],1,null];
   const raw=encode([1,1,read,id,remote?404:200,remote?['missing']:result]);
   current={id,read,raw,digest:await Identity.fullHash(raw),status:remote?404:200};
   if(lostAck)throw Object.assign(Error('timeout'),{code:'TIMEOUT'});
   return [id,read];
  }
  if(op===OP.TRACK_READ_STATUS){assert.deepEqual(body[0],current.id);return [current.id,current.read,current.status,current.raw.length,current.digest];}
  if(op===OP.TRACK_READ_CHUNK){const chunk=current.raw.slice(body[1],body[1]+body[2]);if(corrupt)chunk[0]^=1;return [current.digest,current.raw.length,body[1],chunk];}
  if(op===OP.TRACK_READ_RELEASE){releaseAttempts++;if(busyRelease&&releaseAttempts===1)throw Object.assign(Error('busy'),{code:STATUS.BUSY});current=undefined;return [];}
  assert.fail(`Unexpected operation ${op}`);
 }};
 return {session,calls};
}
const create=(f,extra={})=>createDeviceTrackReadSession({session:f.session,directory,author,operations,assertCurrent:()=>{},sleep:async()=>{},...extra});
test('proven device adapter feeds shared reader with real manifest signature and exact KML',async()=>{
 const f=fixture({busyRelease:true}),adapter=await create(f);
 const result=await readSharedTrack(adapter.request,trackId(manifest.objectId),{chunkBytes:37});
 assert.deepEqual(result.source,source);
 assert.equal(f.calls.filter(([op])=>op===OP.TRACK_READ_BEGIN).length,4);
 assert.ok(f.calls.filter(([op])=>op===OP.TRACK_READ_RELEASE).length>=4);
 adapter.close();await assert.rejects(adapter.request(1,[]),e=>e.code==='TRACK_DIRECTORY_CLOSED');
});
test('lost start ACK is recovered by same request status; corrupt response is never accepted',async()=>{
 const lost=fixture({lostAck:true}),adapter=await create(lost);
 await adapter.request(2,[manifest.objectId,null]);
 assert.equal(lost.calls.filter(([op])=>op===OP.TRACK_READ_BEGIN).length,1);
 const damaged=fixture({corrupt:true}),bad=await create(damaged);
 await assert.rejects(bad.request(2,[manifest.objectId,null]),e=>e.code==='TRACK_DEVICE_READ_HASH_MISMATCH');
 assert.equal(damaged.calls.at(-1)[0],OP.TRACK_READ_RELEASE);
});
test('capability, proof, signed file size and cancellation guards prevent unintended requests',async()=>{
 const f=fixture();await assert.rejects(create(f,{operations:[]}),e=>e.code==='FIRMWARE_UNSUPPORTED');
 await assert.rejects(create(f,{author:{...author,verified:false}}),e=>e.code==='TRACK_DEVICE_READ_OPTIONS_INVALID');
 const adapter=await create(f);
 await assert.rejects(adapter.request(8,[fileHash,0,1]),e=>e.code==='TRACK_FILE_REFERENCE_UNVERIFIED');assert.equal(f.calls.length,0);
 const abort=new AbortController();abort.abort();await assert.rejects(adapter.request(2,[manifest.objectId,null],{signal:abort.signal}),e=>e.name==='AbortError');assert.equal(f.calls.length,0);
 await assert.rejects(adapter.request(4,[]),e=>e.code==='TRACK_DIRECTORY_OPERATION_INVALID');assert.equal(f.calls.length,0);
});
test('directory error is correlated, hashed and released before reporting it',async()=>{
 const f=fixture({remote:true}),adapter=await create(f);
 await assert.rejects(adapter.request(2,[manifest.objectId,null]),e=>e.code==='TRACK_DIRECTORY_REMOTE_ERROR'&&e.status===404&&e.diagnostic==='missing');
 assert.equal(f.calls.at(-1)[0],OP.TRACK_READ_RELEASE);
});
test('cancelling a held ACK releases the accepted operation and ignores its late reply',async()=>{
 const f=fixture(),original=f.session.request;
 let releaseAck,started;
 const acknowledged=new Promise(resolve=>{started=resolve;});
 f.session.request=async(op,body)=>{
  const result=await original(op,body);
  if(op===OP.TRACK_READ_BEGIN){started();return new Promise(resolve=>{releaseAck=()=>resolve(result);});}
  return result;
 };
 const adapter=await create(f),abort=new AbortController();
 const result=adapter.request(2,[manifest.objectId,null],{signal:abort.signal});
 await acknowledged;abort.abort();
 await assert.rejects(result,e=>e.name==='AbortError');
 assert.equal(f.calls.at(-1)[0],OP.TRACK_READ_RELEASE);
 assert.equal(f.calls.some(([op])=>op===OP.TRACK_READ_CHUNK),false);
 releaseAck();adapter.close();
});
