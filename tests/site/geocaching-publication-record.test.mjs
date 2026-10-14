import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {decode,verifySigned} from '../../site/geocaching/src/protocol.js';
import {buildPublicationRecord} from '../../site/geocaching/src/publication-record.js';
import {readDirectoryCurrent} from '../../site/geocaching/src/directory-current.js';
const domain=new TextEncoder().encode('trailmate.geocache/sign/v1\0');
async function fixture(){
 const identity=await Identity.generate(),author={verified:true,publicKey:await identity.getPublicKey(),generation:7};
 const draft={id:new Uint8Array(16).fill(1),authorKey:null,baseHash:null,state:0,latitudeE7:1,longitudeE7:1,name:'Cache',description:'Text',hint:'',difficultyX2:2,terrainX2:2,containerSize:0};
 const first=await buildPublicationRecord(draft,author,{exists:false},{nowSeconds:100});
 const sign=async raw=>{const message=new Uint8Array(domain.length+raw.length);message.set(domain);message.set(raw,domain.length);return [raw,await identity.sign(message)];};
 return {draft,author,first,sign,signed:await sign(first.raw)};
}
test('new, update and archive records bind device author, nonce and signed predecessor',async()=>{
 const f=await fixture(),verified=await verifySigned(f.signed),current={exists:true,signed:f.signed};
 assert.equal(f.first.revision,1);assert.equal(decode(f.first.raw)[14],100);
 f.draft.baseHash=verified.summary[2];f.draft.description='Changed';
 const update=await buildPublicationRecord(f.draft,f.author,current,{nowSeconds:90}),r=decode(update.raw);
 assert.equal(r[3],2);assert.deepEqual(r[4],verified.summary[2]);assert.equal(r[14],100);assert.equal(r[15],100);
 f.draft.state=2;const archive=await buildPublicationRecord(f.draft,f.author,current,{nowSeconds:110});assert.equal(decode(archive.raw)[5],2);
 await assert.rejects(buildPublicationRecord(f.draft,f.author,{exists:true,signed:await f.sign(archive.raw)},{nowSeconds:120}),/stale|Archived/);
});
test('stale or absent base, wrong author and invalid parent signature block record creation',async()=>{
 const f=await fixture();f.draft.baseHash=new Uint8Array(32);
 await assert.rejects(buildPublicationRecord(f.draft,f.author,{exists:true,signed:f.signed}),/stale/);
 await assert.rejects(buildPublicationRecord(f.draft,f.author,{exists:false}),/Existing/);
 f.draft.baseHash=null;f.draft.authorKey=new Uint8Array(64);await assert.rejects(buildPublicationRecord(f.draft,f.author,{exists:false}),/another author/);
 f.draft.authorKey=null;f.signed[1][0]^=1;await assert.rejects(buildPublicationRecord(f.draft,f.author,{exists:true,signed:f.signed}),/signature/i);
});
test('IP current lookup verifies exact cache and accepts absence only from explicit directory 404',async()=>{
 const f=await fixture(),entry={ready:true},client={closed:false,directories:new Map([['d',entry]]),enqueue:fn=>fn(),request:async()=>[f.signed,1,0]};
 assert.equal((await readDirectoryCurrent(client,'d',f.first.cacheId)).exists,true);
 client.request=async()=>{throw Object.assign(Error('Absent'),{status:404,reason:'not_found'});};
 assert.deepEqual(await readDirectoryCurrent(client,'d',f.first.cacheId),{exists:false});
 for(const error of [Error('Timeout'),Object.assign(Error('Conflict'),{status:409,reason:'version_conflict'}),Object.assign(Error('404'),{status:404,reason:'other'})]){
  client.request=async()=>{throw error;};await assert.rejects(readDirectoryCurrent(client,'d',f.first.cacheId),e=>e===error);
 }
 client.request=async()=>[f.signed,1,0];await assert.rejects(readDirectoryCurrent(client,'d',new Uint8Array(32)),/different cache/);
});
