import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {createTrackDirectoryReadSession} from '../../site/tracks/src/directory-read-session.js';
import {createTrackRegionClient} from '../../site/tracks/src/region-client.js';
const feature=(id,parent,box)=>({type:'Feature',id,properties:{name:'Same name',parent,names:{'zh-Hans':'同名区域'}},geometry:{type:'Polygon',coordinates:[[[box[0],box[1]],[box[2],box[1]],[box[2],box[3]],[box[0],box[3]],[box[0],box[1]]]]}});
const regions={type:'FeatureCollection',features:[feature('country',null,[119,29,122,32]),feature('province','country',[119.5,29.5,121.5,31.5]),feature('city','province',[119.9,29.9,121.1,31.1]),feature('district','city',[119.99,29.99,120.5,30.5])]};
test('actual Python directory and signed LXMF serve four-level hierarchy, verified boundary and exact region query',async()=>{
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
 const peer=spawn(python,['-m','tools.tracks.read_interop_fixture'],{stdio:['pipe','pipe','pipe'],windowsHide:true});
 let stderr='';peer.stderr.on('data',bytes=>{stderr=(stderr+bytes.toString()).slice(-10000);});
 const exited=new Promise((resolve,reject)=>{peer.once('exit',code=>resolve(code));peer.once('error',reject);});
 const reader=createInterface({input:peer.stdout})[Symbol.asyncIterator](),next=async()=>{const line=await reader.next();if(line.done)throw Error('Fixture ended: '+stderr);return JSON.parse(line.value);};
 const local=await Identity.generate();peer.stdin.write(JSON.stringify({local_public:Buffer.from(await local.getPublicKey()).toString('hex'),regions})+'\n');
 let session;
 try{
  const fixture=await next(),bin=hex=>new Uint8Array(Buffer.from(hex,'hex'));
  session=await createTrackDirectoryReadSession({directoryPublicKey:bin(fixture.directory_public),directoryDelivery:bin(fixture.directory_delivery),localDelivery:bin(fixture.local_delivery),exchange:async call=>{peer.stdin.write(JSON.stringify({payload:Buffer.from(call.fields.get(0xfc)).toString('hex')})+'\n');return bin((await next()).wire);}});
  const client=await createTrackRegionClient(session.request);assert.equal(client.capabilities.depth,3);
  let parent=null;
  for(let level=0;level<4;level++){
   const page=await client.children(parent);assert.equal(page.items.length,1);assert.equal(page.cursor,null);assert.equal(page.items[0].level,level);
   const region=await client.region(page.items[0].id);assert.equal(region.boundaryVerified,true);assert.equal(region.geometry.type,'MultiPolygon');assert.equal(region.names['zh-Hans'],'同名区域');
   const tracks=await client.query(region);assert.equal(tracks.items.length,1);assert.deepEqual(tracks.items[0].objectId,bin(fixture.id));assert.equal(tracks.items[0].authorVerified,false);
   parent=page.items[0];
  }
  const empty=await client.children(parent);assert.equal(empty.items.length,0);
  await assert.rejects(client.children({...parent,version:new Uint8Array(32)}),e=>e.code==='TRACK_REGION_PAGE_OPTIONS_INVALID');
  await assert.rejects(session.request(7,[]),e=>e.code==='TRACK_DIRECTORY_OPERATION_INVALID');
 }finally{session?.close();peer.stdin.end();assert.equal(await exited,0,stderr);}
});
