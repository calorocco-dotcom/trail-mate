import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {querySharedTrackViewport} from '../../site/tracks/src/shared-track-viewport.js';
import {querySharedTrackRegion} from '../../site/tracks/src/shared-track-region-query.js';
import {queryTrackResultBatch} from '../../site/tracks/src/query-result-batch.js';
import {trackQueryContinuationText} from '../../site/i18n/track-query-continuation-labels.js';
import {locales} from '../../site/i18n/registry.js';
const row=(id,bounds=[0,0,10000000,10000000],title='Trail')=>[new Uint8Array(32).fill(id),new Uint8Array(32).fill(4),1,0,0,title,bounds,[0,null,null,2],120,new Uint8Array(64).fill(8)];
const allBounds=[-10,-10,10,10];
test('items beyond a batch budget are retained without skipping or refetching their page',async()=>{
 let calls=0;const request=async()=>{calls++;return [7,null,[row(1),row(2),row(3)]];};
 const first=await querySharedTrackViewport(request,allBounds,{maxItems:1});assert.equal(first.items[0].objectId[0],1);assert.ok(first.continuation);
 const second=await querySharedTrackViewport(request,allBounds,{maxItems:1,continuation:first.continuation});assert.equal(second.items[0].objectId[0],2);assert.equal(second.pages,0);
 const third=await querySharedTrackViewport(request,allBounds,{maxItems:1,continuation:second.continuation});assert.equal(third.items[0].objectId[0],3);assert.equal(third.continuation,null);assert.equal(calls,1);
 const repeated=await querySharedTrackViewport(request,allBounds,{maxItems:1,continuation:first.continuation});assert.equal(repeated.items[0].objectId[0],2);assert.equal(calls,1);
 await assert.rejects(querySharedTrackViewport(request,allBounds,{limit:0,continuation:first.continuation}),error=>error.code==='TRACK_QUERY_OPTIONS_INVALID');assert.equal(calls,1);
});
test('date-line continuation keeps one snapshot, deduplicates across batches and detects conflicting summaries',async()=>{
 let calls=0;const box=[1750000000,-10000000,-1750000000,10000000],request=async(op,body)=>{calls++;assert.equal(op,1);return [9007199254740993n,null,[row(1,box),...(body[0][0]<0?[row(2,box)]:[])]];};
 const first=await querySharedTrackViewport(request,[-10,170,10,190],{maxPages:1});assert.ok(first.continuation);
 const second=await querySharedTrackViewport(request,[-10,170,10,190],{maxPages:1,continuation:first.continuation});assert.deepEqual(second.items.map(item=>item.objectId[0]),[2]);assert.equal(second.snapshot,first.snapshot);assert.equal(second.continuation,null);assert.equal(calls,2);
 await assert.rejects(querySharedTrackViewport(async()=>[first.snapshot,null,[row(1,box,'Changed')]],[-10,170,10,190],{continuation:first.continuation}),error=>error.code==='TRACK_QUERY_OBJECT_CONFLICT');
});
test('changed area, filters, directory scope and forged handles stop before requests',async()=>{
 const request=async(op,[box,kind,author,state])=>[7,[new Uint8Array(32).fill(1),await Identity.fullHash(encode([box,kind,author,state]))],[row(1)]];
 const first=await querySharedTrackViewport(request,allBounds,{maxPages:1,scopeId:'directory-a'});
 for(const [bounds,options] of [[[0,0,1,1],{}],[allBounds,{state:null}],[allBounds,{activity:3,tags:['x']}],[allBounds,{scopeId:'directory-b'}],[allBounds,{continuation:{type:'track-query-continuation'}}]])await assert.rejects(querySharedTrackViewport(async()=>assert.fail('invalid continuation reached network'),bounds,{scopeId:'directory-a',continuation:first.continuation,...options}),error=>error.code==='TRACK_QUERY_CURSOR_MISMATCH');
});
test('empty region batches retain exact snapshot and cursor, then return later matches',async()=>{
 const region={id:'test:r',version:new Uint8Array(32).fill(1),bounds:[0,0,1,1]};let calls=0;
 const client={query:async(selected,options)=>{assert.equal(selected,region);calls++;if(calls===1)return {snapshot:7,cursor:[new Uint8Array(32).fill(1),new Uint8Array(32).fill(2)],items:[]};assert.equal(options.snapshot,7);assert.equal(options.cursor[0][0],1);return {snapshot:7,cursor:null,items:[{objectId:new Uint8Array(32).fill(2),revisionHash:new Uint8Array(32),revision:1,kind:0,state:0,title:'Later',bounds:[0,0,1,1],statistics:[0,null,null,1],fileBytes:1,author:new Uint8Array(64).fill(1)}]};}};
 const first=await querySharedTrackRegion(client,region,{maxPages:1,activity:3,tags:['山脊']});assert.equal(first.items.length,0);assert.ok(first.continuation);
 const second=await querySharedTrackRegion(client,region,{maxPages:1,activity:3,tags:['山脊'],continuation:first.continuation});assert.equal(second.items[0].title,'Later');assert.equal(second.continuation,null);
 await assert.rejects(querySharedTrackRegion(client,{...region,version:new Uint8Array(32).fill(2)},{activity:3,tags:['山脊'],continuation:first.continuation}),error=>error.code==='TRACK_QUERY_CURSOR_MISMATCH');assert.equal(calls,2);
});
test('late cancellation leaves the prior continuation reusable and all UI locales are complete',async()=>{
 let cancelled=false;const request=async(op,[box,kind,author,state,,snapshot,cursor])=>{if(cursor&&cancelled)controller.abort();return [7,cursor?null:[new Uint8Array(32).fill(1),await Identity.fullHash(encode([box,kind,author,state]))],[row(cursor?2:1)]];};
 const first=await querySharedTrackViewport(request,allBounds,{maxPages:1}),controller=new AbortController();cancelled=true;
 await assert.rejects(querySharedTrackViewport(request,allBounds,{continuation:first.continuation,signal:controller.signal}),error=>error.name==='AbortError');cancelled=false;
 assert.equal((await querySharedTrackViewport(request,allBounds,{continuation:first.continuation})).items[0].objectId[0],2);
 for(const {id} of locales)for(const value of Object.values(trackQueryContinuationText(id)))assert.ok(value.trim());
 const token=await queryTrackResultBatch(async()=>({snapshot:1,cursor:[new Uint8Array(32),new Uint8Array(32)],items:[]}),['context'],{parts:2,maxPages:1});
 await assert.rejects(queryTrackResultBatch(async()=>assert.fail('part count mismatch reached transport'),['context'],{parts:1,continuation:token.continuation}),error=>error.code==='TRACK_QUERY_CURSOR_MISMATCH');
});
