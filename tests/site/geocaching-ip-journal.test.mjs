import test from 'node:test';
import assert from 'node:assert/strict';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {loadIPPublication,saveIPPublication,withIPPublicationLock,discardIPReview} from '../../site/geocaching/src/ip-publication-journal.js';
import {listIPPublicationArchives,restoreIPPublicationArchive} from '../../site/geocaching/src/ip-publication-journal.js';
function fixture(){
 const values=new Map(),storage={getItem:k=>values.get(k)??null,setItem:(k,v)=>values.set(k,v),removeItem:k=>values.delete(k)};
 Object.defineProperty(storage,'length',{get:()=>values.size});storage.key=index=>[...values.keys()][index]??null;
 const authorKey=new Uint8Array(64).fill(1),draftId=new Uint8Array(16).fill(2);
 const record={stage:'reviewed',authorKey,authorGeneration:7,draftId,draftGeneration:1n,operationId:new Uint8Array(16).fill(3),transferId:new Uint8Array(16).fill(4),
  hash:new Uint8Array(32),raw:encode([1,authorKey,draftId,1,null,0,1,1,'Cache','','',2,2,0,1,1]),signature:null,directoryKey:'11'.repeat(16)};
 return {record,storage,values};
}
test('checkpoint preserves exact unsigned bytes and IDs; unresolved work blocks replacement',()=>{
 const f=fixture(),saved=saveIPPublication(f.record,{storage:f.storage});assert.deepEqual(saved.raw,f.record.raw);
 assert.deepEqual(loadIPPublication(f.record.authorKey,{storage:f.storage}).operationId,f.record.operationId);
 assert.throws(()=>saveIPPublication({...f.record,operationId:new Uint8Array(16).fill(5)},{storage:f.storage}),e=>e.code==='PUBLICATION_RETAINED');
 const signed=saveIPPublication({...saved,stage:'signed',signature:new Uint8Array(64).fill(6)},{storage:f.storage,expected:saved});
 assert.throws(()=>saveIPPublication({...saved,stage:'signing'},{storage:f.storage,expected:saved}),e=>e.code==='PUBLICATION_CHECKPOINT_STALE');
 assert.equal(loadIPPublication(f.record.authorKey,{storage:f.storage}).stage,'signed');
 const confirmed=saveIPPublication({...signed,stage:'confirmed'},{storage:f.storage,expected:signed});
 assert.equal(saveIPPublication({...f.record,operationId:new Uint8Array(16).fill(5)},{storage:f.storage}).stage,'reviewed');assert.equal(confirmed.stage,'confirmed');
});
test('history restores exact confirmed content without losing current work',async()=>{
 const f=fixture(),options={storage:f.storage,locks:{request:async(_name,_options,fn)=>fn({})}};
 const first=saveIPPublication({...f.record,stage:'confirmed',signature:new Uint8Array(64).fill(6)},options);
 const second=saveIPPublication({...first,operationId:new Uint8Array(16).fill(5)},options);
 const history=listIPPublicationArchives(first.authorKey,options);assert.equal(history.total,1);assert.deepEqual(history.rows[0].raw,first.raw);
 const restored=await restoreIPPublicationArchive(first.authorKey,first.operationId,options);assert.deepEqual(restored.operationId,first.operationId);
 assert.deepEqual(listIPPublicationArchives(first.authorKey,options).rows[0].operationId,second.operationId);
 saveIPPublication({...restored,stage:'signing',signature:null},{...options,expected:restored});
 await assert.rejects(restoreIPPublicationArchive(first.authorKey,second.operationId,options),e=>e.code==='PUBLICATION_RETAINED');
 assert.equal(loadIPPublication(first.authorKey,options).stage,'signing');
 const archiveName=[...f.values.keys()].find(name=>name.includes('.archive.'));f.values.set(archiveName,'{broken');
 assert.throws(()=>listIPPublicationArchives(first.authorKey,options));
});
test('history pagination follows signed update time rather than random operation ID',()=>{
 const f=fixture(),base={...f.record,stage:'confirmed',signature:new Uint8Array(64).fill(6)},options={storage:f.storage};
 for(const [id,time] of [[9,100],[1,300],[5,200],[8,400]]){
  const fields=decode(base.raw,4096);fields[15]=time;
  saveIPPublication({...base,operationId:new Uint8Array(16).fill(id),raw:encode(fields)},options);
 }
 const first=listIPPublicationArchives(base.authorKey,{...options,limit:2});
 assert.equal(first.total,3);assert.deepEqual(first.rows.map(row=>row.operationId[0]),[1,5]);assert.equal(first.next,2);
 const last=listIPPublicationArchives(base.authorKey,{...options,limit:2,offset:first.next});assert.deepEqual(last.rows.map(row=>row.operationId[0]),[9]);assert.equal(last.next,null);
});
test('discard removes only unchanged unsigned review and preserves started or stale work',async()=>{
 const locks={request:async(_name,_options,fn)=>fn({})};
 for(const stage of ['signing','signed','publishing','unconfirmed','confirmed']){
  const f=fixture(),record=saveIPPublication({...f.record,stage,signature:stage==='signing'?null:new Uint8Array(64).fill(6)},{storage:f.storage});
  await assert.rejects(discardIPReview(record,{storage:f.storage,locks}));assert.equal(loadIPPublication(record.authorKey,{storage:f.storage}).stage,stage);
 }
 const f=fixture(),saved=saveIPPublication(f.record,{storage:f.storage});
 await assert.rejects(discardIPReview({...saved,draftGeneration:2n},{storage:f.storage,locks}));assert.equal(f.values.size,1);
 const task=discardIPReview(saved,{storage:f.storage,locks});saved.authorKey.fill(9);saved.operationId.fill(9);await task;assert.equal(f.values.size,0);
});
test('missing signature, corrupt checkpoint and storage failure do not silently clear work',()=>{
 const f=fixture();assert.throws(()=>saveIPPublication({...f.record,stage:'publishing'},{storage:f.storage}));assert.equal(f.values.size,0);
 saveIPPublication(f.record,{storage:f.storage});const [key]=f.values.keys();f.values.set(key,'{bad');
 assert.throws(()=>loadIPPublication(f.record.authorKey,{storage:f.storage}));assert.equal(f.values.get(key),'{bad');
 assert.throws(()=>saveIPPublication(f.record,{storage:{getItem:()=>null,setItem:()=>{throw Error('Quota');}}}),/Quota/);
});
test('browser lock rejects contention and uses a stable author-specific name',async()=>{
 const f=fixture(),names=[];let calls=0;
 const locks={request:async(name,options,fn)=>{names.push(name);assert.equal(options.ifAvailable,true);return fn({});}};
 await withIPPublicationLock(f.record.authorKey,()=>{calls++;},{locks});await withIPPublicationLock(f.record.authorKey,()=>{calls++;},{locks});
 assert.equal(names[0],names[1]);assert.equal(calls,2);
 await assert.rejects(withIPPublicationLock(f.record.authorKey,()=>{calls++;},{locks:{request:async(_n,_o,fn)=>fn(null)}}),e=>e.code==='PUBLICATION_BUSY');assert.equal(calls,2);
});
