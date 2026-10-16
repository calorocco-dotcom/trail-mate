import test from 'node:test';import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {decode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest,verifyTrackSuccessor} from '../../site/tracks/src/manifest.js';
import {createTrackSuccessorPlan} from '../../site/tracks/src/publication-successor.js';
const source=new TextEncoder().encode('<kml/>');
async function fixture(state=0,revision=1){const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),digest=await Identity.fullHash(source);const record=[1,publicKey,new Uint8Array(16).fill(2),0,revision,revision===1?null:new Uint8Array(32).fill(3),state,100,101,'Track','Original',[0,0,0,0],[0,null,null,1],[digest,source.length,'application/vnd.google-earth.kml+xml',1]],raw=encodeTrackManifest(record);return {identity,author:{verified:true,publicKey,generation:7},signed:[raw,await identity.sign(trackManifestSigningBytes(raw))]};}
function random(){let sequence=10;return size=>new Uint8Array(size).fill(sequence++);}
test('archive retains object and file, increments revision and binds signed parent',async()=>{
 const f=await fixture(),parent=await verifyTrackManifest(f.signed),plan=await createTrackSuccessorPlan(source,f.author,f.signed,{archive:true,updatedAt:102,randomBytes:random()}),row=decode(plan.manifest);
 assert.deepEqual(row[1],parent.record[1]);assert.deepEqual(row[2],parent.record[2]);assert.equal(row[4],2);assert.deepEqual(row[5],parent.revisionHash);assert.equal(row[6],1);assert.equal(row[7],100);assert.equal(row[8],102);assert.deepEqual(row[11],parent.record[11]);assert.deepEqual(row[12],parent.record[12]);assert.deepEqual(row[13],parent.record[13]);
 const next=await verifyTrackManifest([plan.manifest,await f.identity.sign(trackManifestSigningBytes(plan.manifest))]);assert.deepEqual(next.objectId,parent.objectId);assert.equal(verifyTrackSuccessor(parent,next),true);
});
test('archive rejects replaced files, wrong author and invalid signatures',async()=>{
 const f=await fixture(),options={archive:true,updatedAt:102,randomBytes:random()};
 await assert.rejects(createTrackSuccessorPlan(new TextEncoder().encode('<kml> </kml>'),f.author,f.signed,options),/保留原/);
 const other=await fixture();await assert.rejects(createTrackSuccessorPlan(source,other.author,f.signed,options));
 const signature=f.signed[1].slice();signature[0]^=1;await assert.rejects(createTrackSuccessorPlan(source,f.author,[f.signed[0],signature],options));
});
test('archived parents, exhausted revision and time regression cannot create plans',async()=>{
 for(const [state,revision,time] of [[1,2,102],[0,0xffffffff,102],[0,1,100]]){const f=await fixture(state,revision);await assert.rejects(createTrackSuccessorPlan(source,f.author,f.signed,{archive:true,updatedAt:time,randomBytes:random()}));}
});
test('cancelled creation generates no operation IDs',async()=>{
 const f=await fixture(),controller=new AbortController();controller.abort();await assert.rejects(createTrackSuccessorPlan(source,f.author,f.signed,{archive:true,signal:controller.signal,randomBytes(){assert.fail('unexpected IDs');}}),error=>error.name==='AbortError');
});
