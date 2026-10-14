import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {querySharedTrackPage} from '../../site/tracks/src/shared-track-query.js';
import {createTrackRegionClient} from '../../site/tracks/src/region-client.js';
import {createIPTrackReadSession} from '../../site/tracks/src/ip-read-session.js';
const box=[0,0,10000000,10000000],author=new Uint8Array(64).fill(7),tags=['山脊','trail, ridge'];
const row=(activity=3,labels=tags)=>[new Uint8Array(32).fill(1),new Uint8Array(32).fill(2),1,0,0,'Trail',box,[100,null,null,2],200,author,activity,labels];
test('extended map query binds full metadata filters, freezes input and verifies returned annotations',async()=>{
 const input=tags.slice(),binding=await Identity.fullHash(encode(['trailmate.track.query.metadata.v1',box,null,null,0,3,tags]));let pending;
 const query=querySharedTrackPage(async(op,body)=>{assert.equal(op,15);assert.deepEqual(body,[box,null,null,0,3,tags,16,null,null]);return [1,[new Uint8Array(32).fill(1),binding],[row()]];},{box,activity:3,tags:input});input[0]='changed';pending=await query;
 assert.deepEqual(pending.items[0].tags,tags);assert.equal(pending.items[0].activity,3);assert.equal(pending.items[0].authorVerified,false);
 await assert.rejects(querySharedTrackPage(async()=>assert.fail('must reject before request'),{box,activity:2,tags,snapshot:1,cursor:pending.cursor}),error=>error.code==='TRACK_QUERY_CURSOR_MISMATCH');
 for(const item of [row(2),row(3,['山脊']),row(3,['山脊','山脊']),row().slice(0,10)])await assert.rejects(querySharedTrackPage(async()=>[1,null,[item]],{box,activity:3,tags}));
 await assert.rejects(querySharedTrackPage(async()=>assert.fail('invalid filter reached network'),{box,tags:['山'.repeat(17)]}),error=>error.code==='TRACK_QUERY_OPTIONS_INVALID');
});
test('extended region query uses a distinct cursor domain and refuses unsupported capability before querying',async()=>{
 const version=new Uint8Array(32).fill(5),region={id:'test:r',version,level:0,bounds:box};let calls=0;
 const request=async(op,body)=>{if(op===9)return [1,'trailmate.track.regions.v1','test',version,0,32,1024,[9,10,11,12,13]];
  calls++;assert.equal(op,16);assert.deepEqual(body,['test:r',version,'intersects',null,null,0,3,tags,16,null,null]);return [1,null,[row()]];};
 const legacy=await createTrackRegionClient(request);await assert.rejects(legacy.query(region,{activity:3,tags}),error=>error.code==='TRACK_QUERY_METADATA_UNSUPPORTED');assert.equal(calls,0);
 const capable=await createTrackRegionClient(request,{metadataSupported:true});assert.equal((await capable.query(region,{activity:3,tags})).items[0].activity,3);assert.equal(calls,1);
 const oldBinding=await Identity.fullHash(encode(['test:r',version,'intersects',null,null,0]));await assert.rejects(capable.query(region,{activity:3,tags,snapshot:1,cursor:[new Uint8Array(32).fill(1),oldBinding]}),error=>error.code==='TRACK_REGION_QUERY_CURSOR_MISMATCH');assert.equal(calls,1);
});
test('IP adapter checks metadata capabilities and freezes queued filter arrays',async()=>{
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),delivery=new Uint8Array(16).fill(1),epoch=new Uint8Array(16).fill(2),directory={key:'metadata',capabilitiesVerified:true,publicKey,delivery,epoch},entry={identity,destination:delivery,epoch,ready:true,operations:[]};
 const network={generation:1,closed:false,directories:new Map([[directory.key,entry]]),enqueue:async task=>task(),request:async(target,op,body)=>{assert.equal(target,entry);assert.equal(op,15);assert.deepEqual(body[5],tags);return ['ok'];}};
 const legacy=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});assert.throws(()=>legacy.request(15,[]),error=>error.code==='TRACK_QUERY_METADATA_UNSUPPORTED');
 entry.operations=[15,16];const capable=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});assert.equal(capable.capabilities.metadataRegionSupported,true);
 const input=tags.slice(),result=capable.request(15,[box,null,null,0,3,input,16,null,null]);input[0]='changed';assert.deepEqual(await result,['ok']);
});
