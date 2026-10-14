import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {createTrackDirectoryReadSession} from '../../site/tracks/src/directory-read-session.js';
import {readSharedTrack} from '../../site/tracks/src/shared-track-read.js';
import {trackId,trackShareLink} from '../../site/tracks/src/shared-id.js';
import {querySharedTrackPage,sharedTrackViewportBoxes} from '../../site/tracks/src/shared-track-query.js';

test('Python directory application and real LXMF signatures serve direct and pinned KML reads',async()=>{
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
 const peer=spawn(python,['-m','tools.tracks.read_interop_fixture'],{stdio:['pipe','pipe','pipe'],windowsHide:true});
 let stderr='';peer.stderr.on('data',bytes=>{stderr=(stderr+bytes.toString()).slice(-10000);});
 const exited=new Promise((resolve,reject)=>{peer.once('exit',code=>resolve(code));peer.once('error',reject);});
 const reader=createInterface({input:peer.stdout})[Symbol.asyncIterator]();
 const next=async()=>{const line=await reader.next();if(line.done)throw Error('Fixture ended: '+stderr);return JSON.parse(line.value);};
 const local=await Identity.generate();peer.stdin.write(JSON.stringify({local_public:Buffer.from(await local.getPublicKey()).toString('hex')})+'\n');
 let session;
 try{
   const fixture=await next(),bin=hex=>new Uint8Array(Buffer.from(hex,'hex'));
   session=await createTrackDirectoryReadSession({directoryPublicKey:bin(fixture.directory_public),directoryDelivery:bin(fixture.directory_delivery),localDelivery:bin(fixture.local_delivery),exchange:async call=>{peer.stdin.write(JSON.stringify({payload:Buffer.from(call.fields.get(0xfc)).toString('hex')})+'\n');return bin((await next()).wire);}});
   const page=await querySharedTrackPage(session.request,{box:sharedTrackViewportBoxes([29,119,32,122])[0]});assert.equal(page.items.length,1);assert.deepEqual(page.items[0].objectId,bin(fixture.id));assert.equal(page.items[0].authorVerified,false);assert.equal(page.cursor,null);
   const absent=await querySharedTrackPage(session.request,{box:sharedTrackViewportBoxes([0,0,1,1])[0]});assert.equal(absent.items.length,0);
   for(const reference of [trackId(bin(fixture.id)),trackShareLink('https://example.github.io/trail-mate/',bin(fixture.id),{revision:bin(fixture.revision)})]){
     const result=await readSharedTrack(session.request,reference,{chunkBytes:37});assert.equal(result.current,true);assert.deepEqual(result.source,bin(fixture.content));assert.deepEqual(result.revisionHash,bin(fixture.revision));
   }
   await assert.rejects(readSharedTrack(session.request,trackId(new Uint8Array(32).fill(1))),error=>error.code==='TRACK_DIRECTORY_REMOTE_ERROR'&&error.status===422);
 }finally{session?.close();peer.stdin.end();assert.equal(await exited,0,stderr);}
});
