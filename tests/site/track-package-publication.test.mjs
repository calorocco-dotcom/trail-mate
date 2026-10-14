import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {once} from 'node:events';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {encodeTrackAttachmentIndex} from '../../site/tracks/src/attachment-index.js';
import {publishIPTrack} from '../../site/tracks/src/ip-publication.js';
import {uploadIPTrackFile} from '../../site/tracks/src/ip-file-upload.js';
import {validateTrackPackageStatus} from '../../site/tracks/src/package-status.js';
import {TrackDirectoryClient} from '../../site/tracks/src/protocol-client.js';
import {readSharedTrackPackage} from '../../site/tracks/src/shared-track-package.js';
import {trackId} from '../../site/tracks/src/shared-id.js';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
const bin=hex=>new Uint8Array(Buffer.from(hex,'hex')),hex=bytes=>Buffer.from(bytes).toString('hex');
async function broker(){
 const child=spawn(python,['-u','-m','tools.tracks.package_interop'],{stdio:['pipe','pipe','pipe']}),lines=createInterface({input:child.stdout}),pending=new Map();let next=0,errors='';
 let readyResolve,readyReject;const ready=new Promise((resolve,reject)=>{readyResolve=resolve;readyReject=reject;});
 child.stderr.on('data',data=>{errors=(errors+data.toString()).slice(-4096);});child.on('error',readyReject);
 lines.on('line',line=>{const data=JSON.parse(line);if(data.public)readyResolve({publicKey:bin(data.public),epoch:bin(data.epoch)});else{const target=pending.get(data.id);pending.delete(data.id);target?.resolve(data);}});
 const exited=new Promise(resolve=>child.on('exit',(code)=>{const error=Error(errors||'Fixture exited');readyReject(error);for(const target of pending.values())target.reject(error);pending.clear();resolve(code);}));
 const info=await ready,ops=[];
 const ask=message=>new Promise((resolve,reject)=>{const id=++next;pending.set(id,{resolve,reject});child.stdin.write(JSON.stringify({id,...message})+'\n');});
 const request=async(op,body,options={})=>{
  ops.push(op);const id=options.requestId??crypto.getRandomValues(new Uint8Array(16)),response=await ask({kind:'request',raw:hex(encode([1,0,op,id,8192,body]))}),row=decode(bin(response.raw),8192);
  assert.deepEqual(row.slice(0,4),[1,1,op,id]);if(row[4]!==200)throw Object.assign(Error(row[5][0]),{status:row[4],reason:row[5][0]});return row[5];
 };
 return {...info,ops,request,ask,close:async()=>{child.stdin.end();assert.equal(await exited,0,errors);lines.close();}};
}
async function fixture(){
 const author=await Identity.generate(),authorKey=await author.getPublicKey(),source=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><description><![CDATA[<img src="photo.png">]]></description><Point><coordinates>0,0</coordinates></Point></Placemark></kml>');
 const image=new Uint8Array(Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/l9sAAAAASUVORK5CYII=','base64')),notes=new TextEncoder().encode('notes '.repeat(700)),fileHash=await Identity.fullHash(source),imageHash=await Identity.fullHash(image),notesHash=await Identity.fullHash(notes);
 const index=encodeTrackAttachmentIndex([1,fileHash,source.length,[['photo',imageHash,image.length,'image/png','photo.png',0,1],['notes',notesHash,notes.length,'text/plain','notes.txt',3,0]]]);
 const raw=encodeTrackManifest([2,authorKey,new Uint8Array(16).fill(7),0,1,null,0,100,100,'Package','',[0,0,0,0],[0,null,null,1],[fileHash,source.length,'application/vnd.google-earth.kml+xml',1],[1,['hike'],1,0,1,0,0,null],[await Identity.fullHash(index),index.length,2,1]]);
 return {authorKey,source,image,notes,imageHash,notesHash,index,signed:[raw,await author.sign(trackManifestSigningBytes(raw))],requestId:new Uint8Array(16).fill(8)};
}
const options=(b,f,files)=>({authorPublicKey:f.authorKey,directoryPublicKey:b.publicKey,directoryEpoch:b.epoch,requestId:f.requestId,packageSupported:true,attachmentIndex:f.index,attachmentFiles:files});
test('real Python storage accepts required package, reports optional absence and later corruption',async()=>{
 const b=await broker();try{
  const f=await fixture(),result=await publishIPTrack(b.request,f.signed,f.source,options(b,f,[{id:'photo',content:f.image}]));
  assert.equal(result.directoryConfirmed,true);assert.equal(result.availability.requiredComplete,true);assert.equal(result.availability.allComplete,false);
  assert.deepEqual(result.availability.rows.map(row=>row.available),[true,false]);assert.deepEqual(await b.ask({kind:'state'}),{id:b.ops.length+1,versions:1,heads:1});
  const proof=await verifyTrackManifest(f.signed),parsed=decode(f.index,294912);
  await uploadIPTrackFile(b.request,f.notes,f.notesHash);
  assert.equal(validateTrackPackageStatus(await b.request(14,[f.signed]),proof.revisionHash,parsed).allComplete,true);
  await b.ask({kind:'corrupt',hash:hex(f.imageHash)});
  assert.equal(validateTrackPackageStatus(await b.request(14,[f.signed]),proof.revisionHash,parsed).requiredComplete,false);
 }finally{await b.close();}
});
test('shared package reader verifies files from actual Python storage and detects optional corruption',async()=>{
 const b=await broker();try{
  const f=await fixture(),proof=await verifyTrackManifest(f.signed);
  await publishIPTrack(b.request,f.signed,f.source,options(b,f,[{id:'photo',content:f.image}]));
  const packet=await readSharedTrackPackage(b.request,trackId(proof.objectId),{packageSupported:true});
  assert.equal(packet.ready,true);assert.equal(packet.allComplete,false);assert.deepEqual(packet.files[0].content,f.image);assert.deepEqual(packet.attachmentIndex,f.index);
  await uploadIPTrackFile(b.request,f.notes,f.notesHash);await b.ask({kind:'restart'});
  const complete=await readSharedTrackPackage(b.request,trackId(proof.objectId),{packageSupported:true});assert.equal(complete.allComplete,true);assert.deepEqual(complete.files[1].content,f.notes);
  // A false availability bit denotes unavailable bytes; bad bytes returned as
  // available must fail integrity instead (covered by the reader unit test).
  await b.ask({kind:'corrupt',hash:hex(f.notesHash)});const partial=await readSharedTrackPackage(b.request,trackId(proof.objectId),{packageSupported:true});assert.equal(partial.ready,true);assert.equal(partial.rows[1].verified,false);
 }finally{await b.close();}
});
test('missing required file cannot publish; retry reuses completed KML/index without new signature',async()=>{
 const b=await broker();try{
  const f=await fixture(),saved=f.signed.map(value=>value.slice());
  await assert.rejects(publishIPTrack(b.request,f.signed,f.source,options(b,f,[])),/Required track attachments/);
  assert.equal(b.ops.includes(7),false);const state=await b.ask({kind:'state'});assert.equal(state.versions,0);
  const result=await publishIPTrack(b.request,f.signed,f.source,options(b,f,[{id:'photo',content:f.image}]));
  assert.equal(result.directoryConfirmed,true);assert.deepEqual(f.signed,saved);assert.equal(b.ops.filter(op=>op===7).length,1);
 }finally{await b.close();}
});
test('durable resource prefix survives database restart and resumes sequential upload',async()=>{
 const b=await broker();try{
  const f=await fixture();let cut=false;
  const interrupted=async(op,body,settings)=>{const reply=await b.request(op,body,settings);if(op===5&&body[2].length===1024&&!cut){cut=true;throw Error('Lost chunk reply');}return reply;};
  await assert.rejects(publishIPTrack(interrupted,f.signed,f.source,options(b,f,[{id:'photo',content:f.image},{id:'notes',content:f.notes}])),/Lost chunk reply/);
  assert.equal(b.ops.includes(7),false);await b.ask({kind:'restart'});
  let resumed=false;const retry=async(op,body,settings)=>{const response=await b.request(op,body,settings);if(op===4&&hex(body[0])===hex(f.notesHash))resumed=response[1]===1024;return response;};
  const result=await publishIPTrack(retry,f.signed,f.source,options(b,f,[{id:'photo',content:f.image},{id:'notes',content:f.notes}]));
  assert.equal(resumed,true);assert.equal(result.availability.allComplete,true);assert.equal(b.ops.filter(op=>op===7).length,1);
 }finally{await b.close();}
});
test('invalid local bytes and unsupported capability cause no upload; false package status cannot publish',async()=>{
 const b=await broker();try{
  const f=await fixture(),bad=f.image.slice();bad[0]^=1;
  await assert.rejects(publishIPTrack(b.request,f.signed,f.source,options(b,f,[{id:'photo',content:bad}])),/differs from signed index/);assert.equal(b.ops.length,0);
  await assert.rejects(publishIPTrack(b.request,f.signed,f.source,{...options(b,f,[]),packageSupported:false}),/capability/);assert.equal(b.ops.length,0);
  const fake=async(op,body,settings)=>{if(op===14){const proof=await verifyTrackManifest(f.signed);return[1,proof.revisionHash,1,1,1,[]];}return b.request(op,body,settings);};
  await assert.rejects(publishIPTrack(fake,f.signed,f.source,options(b,f,[{id:'photo',content:f.image}])),/Invalid track package status/);assert.equal(b.ops.includes(7),false);
 }finally{await b.close();}
});
test('explicit capability opt-in preserves legacy directories and rejects epoch changes',async()=>{
 const client=new TrackDirectoryClient(),identity=await Identity.generate(),publicKey=await identity.getPublicKey(),epoch=new Uint8Array(16).fill(3),entry={key:'directory',identity,epoch,ready:false};client.directories.set(entry.key,entry);
 const caps=[1,'trailmate.track.v1',publicKey,epoch,8388608,1024,32,[0,1,2,3,4,5,6,7,8]],calls=[];
 client.request=async(_,op,body)=>{calls.push(body);if(body.length)throw Object.assign(Error('operation body'),{status:422});return caps;};await client.checkDirectory(entry);assert.equal(entry.ready,true);assert.equal(entry.operations.includes(14),false);assert.deepEqual(calls,[[],[2]]);
 client.request=async(_,op,body)=>body.length?[...caps.slice(0,7),[...caps[7],14]]:caps;await client.checkDirectory(entry);assert.equal(entry.operations.includes(14),true);
 client.request=async(_,op,body)=>{if(body.length)entry.epoch=new Uint8Array(16).fill(5);return caps;};await assert.rejects(client.checkDirectory(entry),/compatible|changed/);assert.equal(entry.ready,false);
});
