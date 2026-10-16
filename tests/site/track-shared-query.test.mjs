import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {sharedTrackViewportBoxes,querySharedTrackPage} from '../../site/tracks/src/shared-track-query.js';
const box=[1200000000,300000000,1210000000,310000000],author=new Uint8Array(64).fill(7);
const row=(id=1)=>[new Uint8Array(32).fill(id),new Uint8Array(32).fill(8),1,0,0,'Trail',box.slice(),[100,null,null,2],200,author.slice()];
const binding=await Identity.fullHash(encode([box,null,null,0]));

test('viewport conversion preserves world, date-line wrapping and exact boundary coverage',()=>{
 assert.deepEqual(sharedTrackViewportBoxes([30,120,31,121]),[box]);
 assert.deepEqual(sharedTrackViewportBoxes([-10,170,10,190]),[[1700000000,-100000000,1799999999,100000000],[-1800000000,-100000000,-1700000000,100000000]]);
 assert.deepEqual(sharedTrackViewportBoxes([-90,-180,90,180]),[[-1800000000,-900000000,1799999999,900000000]]);
 assert.deepEqual(sharedTrackViewportBoxes([0,179,1,180]),[[1790000000,0,1799999999,10000000],[-1800000000,0,-1800000000,10000000]]);
});
test('pages bind filters and exact snapshots while summaries await author verification',async()=>{
 const result=await querySharedTrackPage(async(op,body)=>{assert.equal(op,1);assert.deepEqual(body,[box,null,null,0,16,null,null]);return [9007199254740993n,[new Uint8Array(32).fill(1),binding],[row()]];},{box});
 assert.equal(result.snapshot,9007199254740993n);assert.equal(result.items[0].authorVerified,false);
 const next=await querySharedTrackPage(async()=>[result.snapshot,null,[row(2)]],{box,snapshot:result.snapshot,cursor:result.cursor});assert.equal(next.items[0].title,'Trail');assert.equal(next.cursor,null);
 await assert.rejects(querySharedTrackPage(async()=>{assert.fail('must reject before request');},{box:[0,0,1,1],snapshot:result.snapshot,cursor:result.cursor}),error=>error.code==='TRACK_QUERY_CURSOR_MISMATCH');
});
test('wrong summary, filter, order, cursor and changed snapshots reject the entire page',async()=>{
 for(const response of [[1,null,[row(2),row(1)]],[1,null,[{bad:true}]],[1,[new Uint8Array(32),new Uint8Array(32)],[row()]],[1,null,[[...row().slice(0,6),[0,0,1,1],...row().slice(7)]]]])await assert.rejects(querySharedTrackPage(async()=>response,{box}));
 await assert.rejects(querySharedTrackPage(async()=>[2,null,[row()]],{box,snapshot:1}),error=>error.code==='TRACK_QUERY_SNAPSHOT_CHANGED');
 await assert.rejects(querySharedTrackPage(async()=>[1,null,[row()]],{box,kind:1}),error=>error.code==='TRACK_QUERY_FILTER_MISMATCH');
});
test('cancelled late results never become a query page',async()=>{
 const abort=new AbortController();await assert.rejects(querySharedTrackPage(async()=>{abort.abort();return [1,null,[row()]];},{box,signal:abort.signal}),error=>error.name==='AbortError');
});

test('first revision cannot be indexed as archived',async()=>{
 const archived=row();archived[4]=1;await assert.rejects(querySharedTrackPage(async()=>[1,null,[archived]],{box,state:null}),error=>error.code==='TRACK_QUERY_SUMMARY_INVALID');
});
