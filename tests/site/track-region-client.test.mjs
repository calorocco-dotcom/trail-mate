import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {createTrackRegionClient} from '../../site/tracks/src/region-client.js';
import {encode} from '../../site/geocaching/src/protocol.js';
const version=new Uint8Array(32).fill(1),row=['test:r',null,0,'Region',[['en','Region']],[1790000000,0,-1790000000,10000000],0];
const boundary=new TextEncoder().encode(JSON.stringify([1,Buffer.from(version).toString('hex'),'test:r',[[[[1790000000,0],[1810000000,0],[1810000000,10000000],[1790000000,10000000],[1790000000,0]]]]]));
const digest=await Identity.fullHash(boundary);
const fixture=change=>async(op,body)=>{
 let result;
 if(op===9)result=[1,'trailmate.track.regions.v1','test',version,0,32,37,[9,10,11,12,13]];
 if(op===10)result=[version,null,[row]];
 if(op===11)result=[version,row,['region-e7-json-v1',digest,boundary.length]];
 if(op===12)result=[digest,boundary.length,body[2],boundary.slice(body[2],body[2]+body[3])];
 if(op===13)result=[9007199254740993n,null,[]];
 return change?.(op,result,body)??result;
};
test('boundary hash, date-line geometry and exact large snapshot are retained',async()=>{
 const client=await createTrackRegionClient(fixture()),page=await client.children(),selected=await client.region(page.items[0].id);
 assert.equal(selected.boundaryVerified,true);assert.deepEqual(selected.geometry.coordinates[0][0][1],[-179,0]);
 assert.equal((await client.query(selected)).snapshot,9007199254740993n);
});
test('parent, version, order and translated name corruption reject entire hierarchy page',async()=>{
 for(const change of [r=>[new Uint8Array(32),r[1],r[2]],r=>[r[0],null,[[...row.slice(0,1),'test:other',...row.slice(2)]]],r=>[r[0],null,[row,row]],r=>[r[0],null,[[...row.slice(0,4),[['en','a'],['en','b']],...row.slice(5)]]]]){
  const client=await createTrackRegionClient(fixture((op,result)=>op===10?change(result):undefined));await assert.rejects(client.children(),e=>['TRACK_REGION_NODE_INVALID','TRACK_REGION_PAGE_INVALID'].includes(e.code));
 }
});
test('corrupt chunk, hash, descriptor and late cancellation never expose boundary geometry',async()=>{
 for(const [modify,code] of [[r=>[r[0],r[1],r[2]+1,r[3]],'TRACK_REGION_BOUNDARY_CHUNK_INVALID'],[r=>{const bytes=r[3].slice();bytes[0]^=1;return [r[0],r[1],r[2],bytes];},'TRACK_REGION_BOUNDARY_HASH_MISMATCH']]){
  const client=await createTrackRegionClient(fixture((op,result)=>op===12?modify(result):undefined));await assert.rejects(client.region('test:r'),e=>e.code===code);
 }
 const wrong=await createTrackRegionClient(fixture((op,result)=>op===11?[version,['test:other',...row.slice(1)],result[2]]:undefined));await assert.rejects(wrong.region('test:r'),e=>e.code==='TRACK_REGION_DESCRIPTOR_INVALID');
 const abort=new AbortController(),cancelled=await createTrackRegionClient(fixture((op)=>{if(op===12)abort.abort();}));await assert.rejects(cancelled.region('test:r',{signal:abort.signal}),e=>e.name==='AbortError');
});
test('region query cursors bind ID, version, relation and filters before sending',async()=>{
 let queryCalls=0;const client=await createTrackRegionClient(fixture(op=>{if(op===13)queryCalls++;})),selected=await client.region('test:r');
 const binding=await Identity.fullHash(encode(['test:r',version,'intersects',null,null,0]));
 await client.query(selected,{snapshot:9007199254740993n,cursor:[new Uint8Array(32).fill(2),binding]});assert.equal(queryCalls,1);
 await assert.rejects(client.query(selected,{state:1,snapshot:9007199254740993n,cursor:[new Uint8Array(32).fill(2),binding]}),e=>e.code==='TRACK_REGION_QUERY_CURSOR_MISMATCH');assert.equal(queryCalls,1);
});
