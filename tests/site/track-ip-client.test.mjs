import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {TrackDirectoryClient} from '../../site/tracks/src/protocol-client.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {trackId} from '../../site/tracks/src/shared-id.js';

test('track IP client pins directory capabilities and dispatches viewport queries',async()=>{
 const client=new TrackDirectoryClient(),identity=await Identity.generate(),key=await identity.getPublicKey(),epoch=new Uint8Array(16).fill(3);
 const entry={key:'directory',identity,destination:new Uint8Array(16).fill(4),epoch,ready:false};client.directories.set(entry.key,entry);
 const caps=[1,'trailmate.track.v1',key,epoch,8388608,1024,32,[0,1,2,3,4,5,6,7,8]];
 client.request=async()=>caps;await client.checkDirectory(entry);assert.equal(entry.ready,true);
 caps[2]=new Uint8Array(64);await assert.rejects(client.checkDirectory(entry),/compatible/);caps[2]=key;
 caps[3]=new Uint8Array(16);await assert.rejects(client.checkDirectory(entry),/compatible/);caps[3]=epoch;assert.equal(entry.ready,false);await client.checkDirectory(entry);
 let queries=0;client.request=async(target,op,body)=>{queries++;assert.equal(target,entry);assert.equal(op,1);assert.deepEqual(body[0],[-10,-10,10,10]);return[1,null,[]];};
 assert.equal((await client.queryPage(entry.key,{box:[-10,-10,10,10]})).items.length,0);assert.equal(queries,1);
 client.directories.delete(entry.key);await assert.rejects(client.queryPage(entry.key,{box:[-10,-10,10,10]}));assert.equal(queries,1);
});

test('track replies cannot be fulfilled by geocaching application envelopes',async()=>{
 const client=new TrackDirectoryClient(),destination=new Uint8Array(16).fill(1),local=new Uint8Array(16).fill(2),id=new Uint8Array(16).fill(3);let replies=0;
 client.router={deliveryDest:{destinationHash:local}};client.pending={directory:{destination,identity:{}},operation:1,id,resolve:()=>replies++,reject:()=>assert.fail('unexpected rejection')};
 const message={sourceHash:destination,destinationHash:local,verifySignature:async()=>true,fields:new Map([[0xfb,'trailmate.geocache.v1'],[0xfc,encode([1,1,1,id,200,[1,null,[]]])]])};
 await client.receive(message);assert.equal(replies,0);message.fields.set(0xfb,'trailmate.track.v1');await client.receive(message);assert.equal(replies,1);
});
test('track IP reader binds manifest author and exact KML file bytes',async()=>{
 const author=await Identity.generate(),source=new TextEncoder().encode('<kml/>'),digest=await Identity.fullHash(source);
 const raw=encodeTrackManifest([1,await author.getPublicKey(),new Uint8Array(16).fill(7),0,1,null,0,100,100,'IP trail','',[0,0,1,1],[0,null,null,2],[digest,source.length,'application/vnd.google-earth.kml+xml',1]]);
 const signed=[raw,await author.sign(trackManifestSigningBytes(raw))],manifest=await verifyTrackManifest(signed),client=new TrackDirectoryClient();
 const entry={ready:true};client.directories.set('directory',entry);let reads=0;
 client.request=async(target,op,body)=>{assert.equal(target,entry);reads++;if(op===2)return[signed,1];assert.equal(op,8);return[digest,source.length,body[1],source.slice(body[1],body[1]+body[2])];};
 const id=trackId(manifest.objectId);assert.deepEqual((await client.readTrack('directory',id)).source,source);assert.equal(reads,2);
 client.request=async(target,op,body)=>op===2?[signed,1]:[digest,source.length,body[1],new Uint8Array(body[2])];
 await assert.rejects(client.readTrack('directory',id),e=>e.code==='TRACK_FILE_HASH_MISMATCH');
});
test('IP region hierarchy pins dataset and revokes context after connection generation changes',async()=>{
 const client=new TrackDirectoryClient(),entry={ready:true,epoch:new Uint8Array(16).fill(3)},version=new Uint8Array(32).fill(4);client.directories.set('directory',entry);let requests=0;
 const root=['test:country',null,0,'Country',[],[0,0,10,10],1];
 client.request=async(target,op,body)=>{requests++;assert.equal(target,entry);if(op===9)return[1,'trailmate.track.regions.v1','test',version,2,16,1024,[9,10,11,12,13]];
  assert.equal(op,10);assert.deepEqual(body,[version,null,null,16]);return[version,null,[root]];
 };
 const regions=await client.regionContext('directory'),page=await regions.children();assert.equal(page.items[0].id,'test:country');assert.equal(requests,2);
 client.generation++;await assert.rejects(regions.children(),/connection changed/);assert.equal(requests,2);
 client.request=async()=>{throw Object.assign(Error('Unsupported'),{status:422,reason:'region capability unsupported'});};
 await assert.rejects(client.regionContext('directory'),error=>error.code==='TRACK_REGION_UNSUPPORTED');
 client.request=async()=>{entry.epoch=new Uint8Array(16).fill(8);return[1,'trailmate.track.regions.v1','test',version,2,16,1024,[9,10,11,12,13]];};
 await assert.rejects(client.regionContext('directory'),/connection changed/);
});
