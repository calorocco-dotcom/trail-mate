import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {createTrackDirectoryReadSession} from '../../site/tracks/src/directory-read-session.js';
import {querySharedTrackPage} from '../../site/tracks/src/shared-track-query.js';
import {createTrackRegionClient} from '../../site/tracks/src/region-client.js';
import {readSharedTrack} from '../../site/tracks/src/shared-track-read.js';
import {trackId} from '../../site/tracks/src/shared-id.js';
test('metadata map/region queries use actual Python directory state and signed LXMF replies',async()=>{
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
 const peer=spawn(python,['-m','tools.tracks.read_interop_fixture'],{stdio:['pipe','pipe','pipe'],windowsHide:true});let stderr='';peer.stderr.on('data',bytes=>stderr+=bytes.toString());
 const exit=new Promise((resolve,reject)=>{peer.once('exit',resolve);peer.once('error',reject);}),reader=createInterface({input:peer.stdout})[Symbol.asyncIterator]();
 const next=async()=>{const line=await reader.next();if(line.done)throw Error('Fixture ended: '+stderr);return JSON.parse(line.value);};
 const local=await Identity.generate(),tags=['山脊','trail, ridge'],regions={type:'FeatureCollection',features:[{type:'Feature',id:'r',properties:{name:'Region'},geometry:{type:'Polygon',coordinates:[[[119,29],[122,29],[122,32],[119,32],[119,29]]]}}]};
 peer.stdin.write(JSON.stringify({local_public:Buffer.from(await local.getPublicKey()).toString('hex'),metadata:[3,tags,0,1,0,0,0,null],regions})+'\n');let session;
 try{
  const fixture=await next(),bin=hex=>new Uint8Array(Buffer.from(hex,'hex'));
  session=await createTrackDirectoryReadSession({metadataQueries:true,directoryPublicKey:bin(fixture.directory_public),directoryDelivery:bin(fixture.directory_delivery),localDelivery:bin(fixture.local_delivery),exchange:async call=>{peer.stdin.write(JSON.stringify({payload:Buffer.from(call.fields.get(0xfc)).toString('hex')})+'\n');return bin((await next()).wire);}});
  assert.ok(session.capabilities.metadataSupported&&session.capabilities.metadataRegionSupported);
  const options={box:[1190000000,290000000,1220000000,320000000],activity:3,tags};
  const map=await querySharedTrackPage(session.request,options);assert.deepEqual(map.items[0].objectId,bin(fixture.id));assert.deepEqual(map.items[0].tags,tags);
  assert.equal((await querySharedTrackPage(session.request,{...options,activity:1})).items.length,0);
  const client=await createTrackRegionClient(session.request,{metadataSupported:true}),root=await client.children(),selected=await client.region(root.items[0].id),page=await client.query(selected,{activity:3,tags});
  assert.deepEqual(page.items[0].objectId,map.items[0].objectId);assert.deepEqual(page.items[0].tags,tags);
  const signed=await readSharedTrack(session.request,trackId(page.items[0].objectId));assert.deepEqual(signed.record[14].slice(0,2),[3,tags]);
  assert.equal((await querySharedTrackPage(session.request,{box:options.box})).items[0].activity,undefined);
 }finally{session?.close();peer.stdin.end();assert.equal(await exit,0,stderr);}
});
