import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {readTrackConflict} from '../../site/tracks/src/track-conflict.js';
import {trackId} from '../../site/tracks/src/shared-id.js';
import {createIPTrackReadSession} from '../../site/tracks/src/ip-read-session.js';
import {TrackDirectoryClient} from '../../site/tracks/src/protocol-client.js';
import {trackConflictText} from '../../site/i18n/track-conflict-labels.js';
import {locales} from '../../site/i18n/registry.js';

async function fixture(){
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey();
 const create=async(title,revision=1,parent=null)=>{const record=[1,publicKey,new Uint8Array(16).fill(4),0,revision,parent,0,100,100,title,'',[0,0,0,0],[0,null,null,1],[new Uint8Array(32).fill(5),100,'application/vnd.google-earth.kml+xml',1]];const raw=encodeTrackManifest(record);return[raw,await identity.sign(trackManifestSigningBytes(raw))];};
 const signed=[await create('Left'),await create('Right')],proofs=await Promise.all(signed.map(value=>verifyTrackManifest(value))),id=trackId(proofs[0].objectId),calls=[];
 const request=async(op,body)=>{calls.push(op);if(op===18)return[proofs[0].objectId,...proofs.map(proof=>proof.revisionHash)];const index=proofs.findIndex(proof=>proof.revisionHash.every((value,i)=>value===body[1][i]));return[signed[index],0];};
 return{identity,publicKey,signed,proofs,id,calls,request,create};
}

test('two exact original author signatures prove a fork without file or USB requests',async()=>{
 const f=await fixture(),proof=await readTrackConflict(f.request,f.id);
 assert.deepEqual(f.calls,[18,2,2]);assert.equal(proof.authorSignaturesVerified,true);assert.equal(proof.chainVerified,false);assert.equal(proof.revision,1);assert.deepEqual(proof.authorFingerprint,await Identity.fullHash(f.publicKey));assert.deepEqual(proof.versions.map(version=>version.record[9]),['Left','Right']);
 assert.ok(Object.isFrozen(proof)&&Object.isFrozen(proof.versions));
});

test('directory assertions, wrong objects, duplicate hashes and bad author signatures are refused',async()=>{
 const f=await fixture();
 for(const value of [[new Uint8Array(32),f.proofs[0].revisionHash,f.proofs[1].revisionHash],[f.proofs[0].objectId,f.proofs[0].revisionHash,f.proofs[0].revisionHash],[f.proofs[0].objectId,f.proofs[0].revisionHash]])await assert.rejects(readTrackConflict(async()=>value,f.id),error=>error.code==='TRACK_CONFLICT_PROOF_INVALID');
 const bad=[f.signed[1][0],new Uint8Array(64)];let index=0;
 await assert.rejects(readTrackConflict(async(op,body)=>op===18?[f.proofs[0].objectId,...f.proofs.map(item=>item.revisionHash)]:[index++===0?f.signed[0]:bad,0],f.id),error=>error.code==='TRACK_CONFLICT_PROOF_INVALID');
 await assert.rejects(readTrackConflict(async(op,body)=>op===18?[f.proofs[0].objectId,...f.proofs.map(item=>item.revisionHash)]:[f.signed[0],0],f.id),error=>error.code==='TRACK_CONFLICT_PROOF_INVALID');
});

test('a normal successor pair cannot be presented as same-revision conflict',async()=>{
 const f=await fixture(),next=await f.create('Revision two',2,f.proofs[0].revisionHash),checked=await verifyTrackManifest(next);let index=0;
 await assert.rejects(readTrackConflict(async(op)=>op===18?[f.proofs[0].objectId,f.proofs[0].revisionHash,checked.revisionHash]:[index++===0?f.signed[0]:next,0],f.id),error=>error.code==='TRACK_CONFLICT_PROOF_INVALID');
});

test('no evidence stops immediately and cancellation discards late replies',async()=>{
 const f=await fixture(),calls=[];assert.equal(await readTrackConflict(async op=>{calls.push(op);return null},f.id),null);assert.deepEqual(calls,[18]);
 const abort=new AbortController();await assert.rejects(readTrackConflict(async()=>{abort.abort();return null},f.id,{signal:abort.signal}),error=>error.name==='AbortError');
});

test('IP operation 18 is capability bound and read-only; remote conflict receives a typed error',async()=>{
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),delivery=new Uint8Array(16).fill(2),epoch=new Uint8Array(16).fill(3),network=new TrackDirectoryClient(),entry={identity,destination:delivery,epoch,ready:true,operations:[]};network.directories.set('directory',entry);
 const directory={key:'directory',publicKey,delivery,epoch,capabilitiesVerified:true};let calls=0;network.request=async()=>{calls++;return null};
 const old=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});assert.equal(old.capabilities.conflictSupported,false);assert.throws(()=>old.request(18,[]),error=>error.code==='TRACK_CONFLICT_UNSUPPORTED');assert.equal(calls,0);
 entry.operations=[18];const capable=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});assert.equal(capable.capabilities.conflictSupported,true);assert.equal(await capable.request(18,[new Uint8Array(32)]),null);assert.equal(calls,1);assert.throws(()=>capable.request(17,[]),/Unsupported/);
 network.request=async()=>{throw Object.assign(Error('Conflict'),{status:422,reason:'track author version conflict'})};await assert.rejects(capable.request(2,[new Uint8Array(32),null]),error=>error.code==='TRACK_AUTHOR_VERSION_CONFLICT');
});

test('conflict messages have independent complete translations for all supported locales',()=>{
 for(const {id} of locales){const text=trackConflictText(id);assert.equal(Object.keys(text).length,12);assert.ok(Object.values(text).every(value=>typeof value==='string'&&value.length>0));}
 assert.throws(()=>trackConflictText('unknown'));assert.notEqual(trackConflictText('ar').help,trackConflictText('en').help);
});
