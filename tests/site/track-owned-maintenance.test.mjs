import test from 'node:test';import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {trackId,trackShareLink} from '../../site/tracks/src/shared-id.js';
import {createOwnedTrackMaintenancePlan} from '../../site/tracks/src/owned-track-maintenance.js';
const source=new TextEncoder().encode('<kml/>');
async function fixture(){const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),digest=await Identity.fullHash(source),raw=encodeTrackManifest([1,publicKey,new Uint8Array(16).fill(2),0,1,null,0,100,100,'Own','',[0,0,0,0],[0,null,null,1],[digest,source.length,'application/vnd.google-earth.kml+xml',1]]),signed=[raw,await identity.sign(trackManifestSigningBytes(raw))],verified=await verifyTrackManifest(signed);return {identity,signed,verified,digest,author:{verified:true,publicKey,generation:7}};}
test('archive reads latest head and exact pinned file before creating successor',async()=>{
 const f=await fixture(),calls=[];let random=10;
 const result=await createOwnedTrackMaintenancePlan(async(op,body)=>{calls.push(op);if(op===2){assert.deepEqual(body[0],f.verified.objectId);assert.equal(body[1]===null,calls.length===1);return [f.signed,1];}return [f.digest,source.length,body[1],source.slice(body[1],body[1]+body[2])];},trackId(f.verified.objectId),f.author,{archive:true,chunkBytes:2,creationOptions:{updatedAt:101,randomBytes:size=>new Uint8Array(size).fill(random++)}});
 assert.deepEqual(calls,[2,2,8,8,8]);assert.equal(result.revision,2);assert.deepEqual(result.parentHash,f.verified.revisionHash);assert.deepEqual(result.source,source);
});
test('a stale pinned revision cannot be maintained',async()=>{
 const f=await fixture();let calls=0;const reference=trackShareLink('https://example.com/',f.verified.objectId,{revision:new Uint8Array(32).fill(9)});
 await assert.rejects(createOwnedTrackMaintenancePlan(async(op,body)=>{calls++;assert.equal(body[1],null);return [f.signed,1];},reference,f.author,{archive:true}),error=>error.code==='TRACK_HEAD_CHANGED');assert.equal(calls,1);
});
test('other identities and unconfirmed heads stop before file reads',async()=>{
 const f=await fixture(),other=await Identity.generate();
 for(const [author,current,code] of [[{...f.author,publicKey:await other.getPublicKey()},1,'TRACK_NOT_OWNED'],[f.author,0,'TRACK_HEAD_UNCONFIRMED']]){let calls=0;await assert.rejects(createOwnedTrackMaintenancePlan(async()=>{calls++;return [f.signed,current];},trackId(f.verified.objectId),author,{archive:true}),error=>error.code===code);assert.equal(calls,1);}
});
test('a head change between metadata and pinned archive read rejects creation',async()=>{
 const f=await fixture();let metadata=0;await assert.rejects(createOwnedTrackMaintenancePlan(async(op,body)=>op===2?[f.signed,++metadata===1?1:0]:[f.digest,source.length,body[1],source.slice(body[1],body[1]+body[2])],trackId(f.verified.objectId),f.author,{archive:true}),error=>error.code==='TRACK_HEAD_CHANGED');
});

test('same author restored with a new device generation maintains the original object and predecessor',async()=>{
 const f=await fixture(),restored={...f.author,generation:8};let random=30;
 const result=await createOwnedTrackMaintenancePlan(async(op,body)=>op===2?[f.signed,1]:[f.digest,source.length,body[1],source.slice(body[1],body[1]+body[2])],
  trackId(f.verified.objectId),restored,{archive:true,chunkBytes:2,creationOptions:{updatedAt:102,randomBytes:size=>new Uint8Array(size).fill(random++)}});
 assert.equal(result.plan.generation,8);assert.equal(result.revision,2);assert.equal(result.archive,true);
 assert.deepEqual(result.objectId,f.verified.objectId);assert.deepEqual(result.parentHash,f.verified.revisionHash);
 assert.deepEqual(result.previousSigned,f.signed);assert.deepEqual(result.source,source);
});
