import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {verifySigned} from '../../site/geocaching/src/protocol.js';
import {decode} from '../../site/geocaching/src/protocol.js';
import {loadIPPublication,saveIPPublication} from '../../site/geocaching/src/ip-publication-journal.js';
import {reviewIPPublication,signIPPublication,publishIPPublication} from '../../site/geocaching/src/ip-publication-workflow.js';
async function fixture(){
 const identity=await Identity.generate(),author={verified:true,publicKey:await identity.getPublicKey(),generation:7};
 const values=new Map(),storage={getItem:k=>values.get(k)??null,setItem:(k,v)=>values.set(k,v)},locks={request:async(_n,_o,fn)=>fn({})};
 const options={storage,locks,nowSeconds:100,randomBytes:()=>crypto.getRandomValues(new Uint8Array(16)),operations:[32,33,35,71,72,73]};
 const snapshot={draft:{id:new Uint8Array(16).fill(1),generation:1n,authorKey:null,baseHash:null,state:0,latitudeE7:1,longitudeE7:1,name:'Cache',description:'Text',hint:'',difficultyX2:2,terrainX2:2,containerSize:0}};
 const directory={key:'11'.repeat(16),capabilitiesVerified:true},calls=[];
 const reader={async signRecord(raw,_author,settings){
  assert.equal(loadIPPublication(author.publicKey,{storage}).stage,'signing');calls.push({type:'sign',raw:raw.slice(),operationId:settings.operationId.slice()});
  const domain=new TextEncoder().encode('trailmate.geocache/sign/v1\0'),message=new Uint8Array(domain.length+raw.length);message.set(domain);message.set(raw,domain.length);
  return {signed:[raw.slice(),await identity.sign(message)]};
 },async clearSignatureTransfer(id){calls.push({type:'clear',id:id.slice()});}};
 const network={ready:()=>true,current:async()=>({exists:false}),async publish(signed){
  assert.equal(loadIPPublication(author.publicKey,{storage}).stage,'publishing');calls.push({type:'publish',signed});
  const verified=await verifySigned(signed);return {confirmed:true,cacheId:verified.cacheId,revision:verified.record[3],revisionHash:verified.revisionHash,state:verified.record[5]};
 }};
 return {author,options,snapshot,directory,reader,network,calls,storage};
}
test('review, device signature and browser IP publish persist before side effects',async()=>{
 const f=await fixture(),review=await reviewIPPublication(f.reader,f.snapshot,f.author,f.directory,f.network,f.options);
 const signed=await signIPPublication(f.reader,review,f.author,f.options),published=await publishIPPublication(f.reader,signed,f.author,f.network,f.options);
 assert.equal(published.record.stage,'confirmed');assert.equal(published.cleanupError,null);assert.deepEqual(f.calls.map(c=>c.type),['sign','publish','clear']);
 assert.deepEqual(published.record.raw,review.raw);assert.deepEqual(published.record.operationId,review.operationId);
});
test('lost IP receipt preserves exact signed content; resume never requests another device signature',async()=>{
 const f=await fixture(),review=await reviewIPPublication(f.reader,f.snapshot,f.author,f.directory,f.network,f.options),signed=await signIPPublication(f.reader,review,f.author,f.options);
 const publish=f.network.publish;let lost=true;
 f.network.publish=async value=>{if(lost){lost=false;throw Error('Lost receipt');}return publish(value);};
 await assert.rejects(publishIPPublication(f.reader,signed,f.author,f.network,f.options),/Lost/);
 const retained=loadIPPublication(f.author.publicKey,{storage:f.storage});assert.equal(retained.stage,'unconfirmed');
 const restored={...f.author,generation:8};
 const confirmed=await publishIPPublication(f.reader,retained,restored,f.network,f.options);
 assert.equal(confirmed.record.authorGeneration,7);assert.deepEqual(confirmed.record.operationId,retained.operationId);assert.deepEqual(confirmed.record.signature,retained.signature);
 assert.equal(f.calls.filter(c=>c.type==='clear').length,0,'restored account must not clean old-generation USB uploads');
 assert.equal(f.calls.filter(c=>c.type==='sign').length,1);assert.deepEqual(retained.raw,review.raw);
});
test('checkpoint failure prevents signing; wrong network receipt cannot confirm',async()=>{
 const f=await fixture(),review=await reviewIPPublication(f.reader,f.snapshot,f.author,f.directory,f.network,f.options);
 const failing={...f.options,storage:{getItem:f.storage.getItem,setItem:()=>{throw Error('Quota');}}};
 await assert.rejects(signIPPublication(f.reader,review,f.author,failing),/Quota/);assert.equal(f.calls.length,0);
 const signed=await signIPPublication(f.reader,review,f.author,f.options);f.network.publish=async()=>({confirmed:true,cacheId:'wrong'});
 await assert.rejects(publishIPPublication(f.reader,signed,f.author,f.network,f.options),/binding/);assert.equal(loadIPPublication(f.author.publicKey,{storage:f.storage}).stage,'unconfirmed');
});
test('review freezes draft and device author before asynchronous directory lookup',async()=>{
 const f=await fixture(),publicKey=f.author.publicKey.slice(),id=f.snapshot.draft.id.slice();
 const work=reviewIPPublication(f.reader,f.snapshot,f.author,f.directory,f.network,f.options);
 f.author.publicKey.fill(9);f.author.generation=8;f.snapshot.draft.id.fill(9);f.snapshot.draft.name='Changed';f.snapshot.draft.generation=2n;
 const result=await work;assert.deepEqual(result.authorKey,publicKey);assert.deepEqual(result.draftId,id);
 assert.equal(result.authorGeneration,7);assert.equal(result.draftGeneration,1n);assert.equal(decode(result.raw)[8],'Cache');
});

test('same author at a new generation can review a successor without deleting old uploads',async()=>{
 const f=await fixture(),review=await reviewIPPublication(f.reader,f.snapshot,f.author,f.directory,f.network,f.options);
 const signed=await signIPPublication(f.reader,review,f.author,f.options),published=await publishIPPublication(f.reader,signed,f.author,f.network,f.options);
 f.calls.length=0;
 const restored={...f.author,generation:8},parent=await verifySigned([published.record.raw,published.record.signature]);
 f.network.current=async()=>({exists:true,signed:[published.record.raw,published.record.signature]});
 const nextSnapshot={draft:{...f.snapshot.draft,authorKey:f.author.publicKey,baseHash:parent.summary[2],generation:2n,name:'Updated'}};
 const next=await reviewIPPublication(f.reader,nextSnapshot,restored,f.directory,f.network,f.options);
 assert.equal(next.authorGeneration,8);assert.equal(decode(next.raw)[3],2);assert.deepEqual(decode(next.raw)[4],parent.summary[2]);
 assert.deepEqual(next.draftId,published.record.draftId);assert.deepEqual(f.calls,[]);
 await assert.rejects(signIPPublication(f.reader,next,f.author,f.options),/identity changed/);
});

test('restored network resumption requires the original author and a valid signature',async()=>{
 const f=await fixture(),review=await reviewIPPublication(f.reader,f.snapshot,f.author,f.directory,f.network,f.options);
 const signed=await signIPPublication(f.reader,review,f.author,f.options),restored={...f.author,generation:8};
 const before=f.calls.length;
 await assert.rejects(publishIPPublication(f.reader,signed,{...restored,generation:0},f.network,f.options));
 await assert.rejects(publishIPPublication(f.reader,signed,{...restored,publicKey:await (await Identity.generate()).getPublicKey()},f.network,f.options));
 const bad=saveIPPublication({...signed,signature:new Uint8Array(64)},{...f.options,expected:signed});
 await assert.rejects(publishIPPublication(f.reader,bad,restored,f.network,f.options));
 assert.equal(f.calls.length,before);assert.equal(loadIPPublication(f.author.publicKey,f.options).stage,'signed');
 const unsigned={...review,stage:'reviewed'};
 await assert.rejects(signIPPublication(f.reader,unsigned,restored,f.options),/identity changed/);
});
