import test from 'node:test';
import assert from 'node:assert/strict';
import {withTrackDraftLock} from '../../site/tracks/src/draft-lock.js';
test('draft lock has stable per-draft names and contention never enters work',async()=>{
 const names=[];let entered=0;const locks={request:async(name,options,work)=>{names.push(name);assert.equal(options.ifAvailable,true);return work({});}};
 await withTrackDraftLock('draft-a',()=>entered++,{locks});await withTrackDraftLock('draft-a',()=>entered++,{locks});await withTrackDraftLock('draft-b',()=>entered++,{locks});assert.equal(names[0],names[1]);assert.notEqual(names[0],names[2]);assert.equal(entered,3);
 await assert.rejects(withTrackDraftLock('draft-a',()=>entered++,{locks:{request:async(_name,_options,work)=>work(null)}}),error=>error.code==='TRACK_IP_DRAFT_BUSY');assert.equal(entered,3);
 await assert.rejects(withTrackDraftLock('draft-a',()=>entered++,{locks:null}));assert.equal(entered,3);
});
