import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {querySharedTrackViewport} from '../../site/tracks/src/shared-track-viewport.js';
const row=(id,bounds=[0,-10000000,10000000,10000000],title='Trail')=>[new Uint8Array(32).fill(id),new Uint8Array(32).fill(4),1,0,0,title,bounds,[0,null,null,2],120,new Uint8Array(64).fill(8)];
test('split date-line boxes share exact snapshot and deduplicate matching rows',async()=>{
 const calls=[],snapshot=9007199254740993n;
 const result=await querySharedTrackViewport(async(op,body)=>{assert.equal(op,1);calls.push(body);return [snapshot,null,[row(1,[1750000000,-10000000,-1750000000,10000000])]];},[-10,170,10,190]);
 assert.equal(result.items.length,1);assert.equal(result.truncated,false);assert.equal(result.snapshot,snapshot);assert.equal(calls.length,2);assert.equal(calls[0][5],null);assert.equal(calls[1][5],snapshot);
});
test('pagination stays in one snapshot and reports an explicit bounded result',async()=>{
 const transport=async(op,[box,kind,author,state,,snapshot,cursor])=>{
  assert.equal(op,1);const binding=await Identity.fullHash(encode([box,kind,author,state]));
  return cursor?[7,null,[row(2)]]:[7,[new Uint8Array(32).fill(1),binding],[row(1)]];
 };
 const complete=await querySharedTrackViewport(transport,[-10,-10,10,10],{limit:1});assert.equal(complete.items.length,2);assert.equal(complete.pages,2);assert.equal(complete.truncated,false);
 const limited=await querySharedTrackViewport(transport,[-10,-10,10,10],{limit:1,maxPages:1});assert.equal(limited.items.length,1);assert.equal(limited.truncated,true);
});
test('conflicting duplicate, changed snapshot and cancelled late reply never return a merged page',async()=>{
 let calls=0;
 await assert.rejects(querySharedTrackViewport(async()=>[7,null,[row(1,[1750000000,-10000000,-1750000000,10000000],++calls===1?'Trail':'Changed')]],[-10,170,10,190]),e=>e.code==='TRACK_QUERY_OBJECT_CONFLICT');
 calls=0;await assert.rejects(querySharedTrackViewport(async()=>[++calls,null,[]],[-10,170,10,190]),e=>e.code==='TRACK_QUERY_SNAPSHOT_CHANGED');
 const abort=new AbortController();await assert.rejects(querySharedTrackViewport(async()=>{abort.abort();return [7,null,[]];},[-10,-10,10,10],{signal:abort.signal}),e=>e.name==='AbortError');
});
