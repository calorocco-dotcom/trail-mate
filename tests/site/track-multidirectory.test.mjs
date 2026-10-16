import test from 'node:test';
import assert from 'node:assert/strict';
import {queryTrackDirectories} from '../../site/tracks/src/multidirectory-query.js';
import {trackMultidirectoryText} from '../../site/i18n/track-multidirectory-labels.js';
import {locales} from '../../site/i18n/registry.js';
const directory=value=>({key:'node-'+value,name:'Directory '+value,capabilitiesVerified:true,publicKey:new Uint8Array(64).fill(value),delivery:new Uint8Array(16).fill(value),epoch:new Uint8Array(16).fill(8)});
const row=(id,revision=1,title='Track '+id)=>({objectId:new Uint8Array(32).fill(id),revisionHash:new Uint8Array(32).fill(revision),revision,kind:0,state:0,title,bounds:[0,0,0,0],statistics:[0,null,null,1],fileBytes:123,author:new Uint8Array(64).fill(7),authorVerified:false});
const context=['viewport',[0,0,1,1],null,0,null,[]];

test('multidirectory status and source choice have complete text in eleven locales',()=>{
 for(const {id} of locales){const text=trackMultidirectoryText(id);assert.equal(Object.keys(text).length,19);assert.ok(Object.values(text).every(value=>typeof value==='string'&&value.length));assert.notEqual(text.versions,text.disagreement);assert.notEqual(text.complete,text.partial);}
});

test('exact summaries merge providers; different versions and differing summaries are retained without a winner',async()=>{
 const sources=[directory(1),directory(2),directory(3)],result=await queryTrackDirectories(async source=>({items:source.key==='node-1'?[row(1)]:source.key==='node-2'?[row(1),row(2)]:[row(1,2),row(2,1,'Other claim')],continuation:null}),sources,context);
 assert.equal(result.items.length,2);assert.equal(result.items[0].variants.length,2);assert.equal(result.items[0].multipleVersions,true);assert.equal(result.items[0].variants[0].sources.length,2);assert.equal(result.items[1].summaryDisagreement,true);assert.equal(result.items[1].variants.length,2);assert.ok(result.items.every(item=>item.authorVerified===false));assert.equal(result.truncated,false);
});

test('a round visits at most eight sources and continues fairly without repeating completed sources',async()=>{
 const sources=Array.from({length:10},(_,i)=>directory(i+1)),calls=[],read=async source=>{calls.push(source.key);return{items:[row(Number(source.key.split('-')[1]))],continuation:null};};
 const first=await queryTrackDirectories(read,sources,context);assert.equal(calls.length,8);assert.equal(first.items.length,8);assert.ok(first.continuation);assert.equal(first.sources.filter(value=>value.status==='pending').length,2);
 const final=await queryTrackDirectories(read,sources,context,{continuation:first.continuation});assert.equal(final.items.length,10);assert.equal(final.continuation,null);assert.equal(new Set(calls).size,10);
});

test('each source retains its own continuation; a missing source does not erase other results and can be retried',async()=>{
 const sources=[directory(1),directory(2)],cursor={},calls=[];let unavailable=true;
 const read=async(source,{continuation})=>{calls.push([source.key,continuation]);if(source.key==='node-2'&&unavailable)throw Object.assign(Error('503'),{code:'TRACK_DIRECTORY_STORAGE_UNAVAILABLE'});if(source.key==='node-1'&&continuation===null)return{items:[row(1)],continuation:cursor};return{items:[row(source.key==='node-1'?3:2)],continuation:null};};
 const first=await queryTrackDirectories(read,sources,context);assert.equal(first.partial,true);assert.equal(first.items.length,1);assert.ok(first.retry);assert.ok(first.continuation);
 const second=await queryTrackDirectories(read,sources,context,{continuation:first.continuation});assert.deepEqual(calls.at(-1),['node-1',cursor]);assert.equal(second.items.length,2);assert.equal(second.continuation,null);assert.ok(second.retry);
 unavailable=false;const final=await queryTrackDirectories(read,sources,context,{continuation:second.retry,retryFailed:true});assert.equal(final.items.length,3);assert.equal(final.partial,false);assert.equal(final.truncated,false);assert.equal(calls.filter(value=>value[0]==='node-1').length,2);
});

test('window limits retain pending items; global dedup does not omit distinct later results',async()=>{
 const sources=[directory(1),directory(2)],read=async source=>({items:source.key==='node-1'?[row(1),row(2),row(3)]:[row(1),row(4)],continuation:null});
 const one=await queryTrackDirectories(read,sources,context,{maxItems:2});assert.equal(one.items.length,2);assert.equal(one.windowFull,true);
 const two=await queryTrackDirectories(read,sources,context,{maxItems:2,continuation:one.continuation});assert.equal(two.items.length,2);assert.deepEqual(two.items.map(value=>value.objectId[0]),[3,4]);assert.equal(two.continuation,null);
});

test('a source snapshot change is explicit and never retried with a silently replaced snapshot',async()=>{
 const sources=[directory(1),directory(2)],read=async source=>{if(source.key==='node-2')throw Object.assign(Error('changed'),{code:'TRACK_QUERY_SNAPSHOT_CHANGED'});return{items:[row(1)],continuation:null};};
 const result=await queryTrackDirectories(read,sources,context);assert.equal(result.items.length,1);assert.equal(result.partial,true);assert.equal(result.retry,null);assert.equal(result.sources[1].retryable,false);assert.equal(result.sources[1].error,'TRACK_QUERY_SNAPSHOT_CHANGED');
});

test('opaque continuations bind every source identity/epoch, query and output budget',async()=>{
 const sources=[directory(1),directory(2)],read=async()=>({items:[],continuation:{}}),first=await queryTrackDirectories(read,sources,context);
 for(const [changed,scope,options] of [[[{...sources[0],epoch:new Uint8Array(16).fill(9)},sources[1]],context,{}],[sources,['other'],{}],[sources,context,{maxItems:1}],[sources.slice(1),context,{}]])await assert.rejects(queryTrackDirectories(read,changed,scope,{...options,continuation:first.continuation}),{code:'TRACK_QUERY_CURSOR_MISMATCH'});
 await assert.rejects(queryTrackDirectories(read,sources,context,{continuation:{type:'track-multidirectory-continuation'}}),{code:'TRACK_QUERY_CURSOR_MISMATCH'});
 await assert.rejects(queryTrackDirectories(read,[sources[0],sources[0]],context),{code:'TRACK_MULTIDIRECTORY_OPTIONS_INVALID'});
});

test('cancelled/late batches do not mutate an older continuation, and progress consumers cannot change retained summaries',async()=>{
 const sources=[directory(1)],cursor={},first=await queryTrackDirectories(async()=>({items:[row(1)],continuation:cursor}),sources,context),controller=new AbortController();
 await assert.rejects(queryTrackDirectories(async()=>{controller.abort();return{items:[row(2)],continuation:null};},sources,context,{continuation:first.continuation,signal:controller.signal}),{name:'AbortError'});
 const final=await queryTrackDirectories(async()=>({items:[row(2)],continuation:null}),sources,context,{continuation:first.continuation,onProgress:value=>{value.items[0].objectId.fill(99);value.items[0].variants[0].sources[0].epoch.fill(99);}});assert.deepEqual(final.items.map(value=>value.objectId[0]),[1,2]);assert.equal(first.items[0].objectId[0],1);
});

test('malformed summaries are excluded and the dedup budget is reported as incomplete',async()=>{
 const sources=[directory(1),directory(2)],result=await queryTrackDirectories(async source=>({items:source.key==='node-1'?[row(1),row(2),row(3)]:[{...row(4),fileBytes:0}],continuation:null}),sources,context,{maxItems:2,maxSeen:2});
 assert.equal(result.items.length,2);const next=await queryTrackDirectories(async()=>assert.fail('Pending items should be retained'),sources,context,{maxItems:2,maxSeen:2,continuation:result.continuation});assert.equal(next.capacityReached,true);assert.equal(next.truncated,true);assert.equal(next.continuation,null);
});
