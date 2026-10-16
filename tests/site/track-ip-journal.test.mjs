import test from 'node:test';
import assert from 'node:assert/strict';
import {encodeTrackManifest} from '../../site/tracks/src/manifest.js';
import {loadTrackIPCheckpoint,saveTrackIPCheckpoint,encodeTrackIPCheckpoint,assertTrackIPDraftEditable,discardTrackIPReview} from '../../site/tracks/src/ip-publication-journal.js';
import {listTrackIPCheckpoints} from '../../site/tracks/src/ip-publication-journal.js';
import {decode} from '../../site/geocaching/src/protocol.js';
const bin=(size,value)=>new Uint8Array(size).fill(value);
function fixture(){
 const values=new Map(),storage={getItem:key=>values.get(key)??null,setItem:(key,value)=>values.set(key,value)},authorKey=bin(64,1),fileHash=bin(32,2);
 Object.defineProperty(storage,'length',{get:()=>values.size});storage.key=index=>[...values.keys()][index]??null;storage.removeItem=key=>values.delete(key);
 const manifest=encodeTrackManifest([1,authorKey,bin(16,3),0,1,null,0,100,100,'Checkpoint trail','',[0,0,1,1],[0,null,null,2],[fileHash,2000,'application/vnd.google-earth.kml+xml',1]]);
 const record={stage:'reviewed',draftId:'draft-one',draftVersion:1,authorKey,requestId:bin(16,4),directoryKey:bin(64,5),delivery:bin(16,6),epoch:bin(16,7),
  plan:{operationId:bin(16,8),kmlUploadId:bin(16,9),manifestUploadId:bin(16,10),generation:7,manifest,fileHash,fileBytes:2000},signature:null,receipt:null};
 return{record,storage,values};
}
test('track checkpoint resumes exact plan and IDs while signed content cannot change',()=>{
 const f=fixture(),options={storage:f.storage},reviewed=saveTrackIPCheckpoint(f.record,options);
 assert.deepEqual(loadTrackIPCheckpoint(f.record.authorKey,'draft-one',1,options).plan.manifest,f.record.plan.manifest);
 assert.throws(()=>saveTrackIPCheckpoint(f.record,options),error=>error.code==='TRACK_IP_RETAINED');
 const signing=saveTrackIPCheckpoint({...reviewed,stage:'signing'},{...options,expected:reviewed});
 const signed=saveTrackIPCheckpoint({...signing,stage:'signed',signature:bin(64,11)},{...options,expected:signing});
 for(const patch of [{requestId:bin(16,12)},{epoch:bin(16,12)},{signature:bin(64,12)},{plan:{...signed.plan,generation:8}}])assert.throws(()=>saveTrackIPCheckpoint({...signed,...patch},{...options,expected:signed}),/binding changed/);
 assert.throws(()=>saveTrackIPCheckpoint({...reviewed,stage:'signing'},{...options,expected:reviewed}),error=>error.code==='TRACK_IP_CHECKPOINT_STALE');
 const publishing=saveTrackIPCheckpoint({...signed,stage:'publishing'},{...options,expected:signed});
 const unconfirmed=saveTrackIPCheckpoint({...publishing,stage:'unconfirmed'},{...options,expected:publishing});
 const resumed=loadTrackIPCheckpoint(signed.authorKey,'draft-one',1,options);assert.deepEqual(resumed.requestId,signed.requestId);assert.deepEqual(resumed.signature,signed.signature);assert.equal(resumed.stage,'unconfirmed');
 assert.throws(()=>saveTrackIPCheckpoint({...unconfirmed,stage:'reviewed',signature:null},{...options,expected:unconfirmed}),/binding changed|transition/);
 assert.ok(encodeTrackIPCheckpoint(resumed).length<2000);
});
test('checkpoint storage errors and invalid signatures never clear retained work',()=>{
 const f=fixture();assert.throws(()=>saveTrackIPCheckpoint({...f.record,stage:'signed'},{storage:f.storage}));assert.equal(f.values.size,0);
 const saved=saveTrackIPCheckpoint(f.record,{storage:f.storage}),[key]=f.values.keys(),before=f.values.get(key);
 assert.throws(()=>saveTrackIPCheckpoint({...saved,stage:'signing'},{expected:saved,storage:{getItem:()=>before,setItem:()=>{throw Error('Quota');}}}),/Quota/);assert.equal(f.values.get(key),before);
 f.values.set(key,'bad base64');assert.throws(()=>loadTrackIPCheckpoint(saved.authorKey,saved.draftId,saved.draftVersion,{storage:f.storage}));assert.equal(f.values.get(key),'bad base64');
});
test('unfinished publication locks its draft and only unchanged unsigned review can be discarded',async()=>{
 const f=fixture(),options={storage:f.storage,locks:{request:async(_name,_options,fn)=>fn({})}},reviewed=saveTrackIPCheckpoint(f.record,options);
 assert.throws(()=>assertTrackIPDraftEditable('draft-one',1,options),error=>error.code==='TRACK_IP_DRAFT_LOCKED');assert.doesNotThrow(()=>assertTrackIPDraftEditable('other-draft',1,options));
 await assert.rejects(discardTrackIPReview({...reviewed,requestId:bin(16,12)},options));assert.equal(f.values.size,1);
 await discardTrackIPReview(reviewed,options);assert.equal(f.values.size,0);assert.doesNotThrow(()=>assertTrackIPDraftEditable('draft-one',1,options));
 const signing=saveTrackIPCheckpoint({...reviewed,stage:'signing'},options);await assert.rejects(discardTrackIPReview(signing,options));assert.throws(()=>assertTrackIPDraftEditable('draft-one',1,options),error=>error.code==='TRACK_IP_DRAFT_LOCKED');
});
test('history isolates authors and paginates by signed manifest update time',()=>{
 const f=fixture(),options={storage:f.storage};
 for(const [id,time] of [['older',100],['newest',300],['middle',200]]){
  const fields=decode(f.record.plan.manifest,4096);fields[8]=time;
  saveTrackIPCheckpoint({...f.record,draftId:id,plan:{...f.record.plan,manifest:encodeTrackManifest(fields)}},options);
 }
 const first=listTrackIPCheckpoints(f.record.authorKey,{...options,limit:2});assert.equal(first.total,3);assert.deepEqual(first.rows.map(row=>row.draftId),['newest','middle']);assert.equal(first.next,2);
 const last=listTrackIPCheckpoints(f.record.authorKey,{...options,limit:2,offset:first.next});assert.deepEqual(last.rows.map(row=>row.draftId),['older']);assert.equal(last.next,null);
 assert.equal(listTrackIPCheckpoints(bin(64,9),options).total,0);
 const [name]=f.values.keys();f.values.set(name,'corrupted');assert.throws(()=>listTrackIPCheckpoints(f.record.authorKey,options));
});
