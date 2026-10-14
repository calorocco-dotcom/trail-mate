import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import path from 'node:path';
import {encode,decode,equal} from '../../site/geocaching/src/protocol.js';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {signDeviceTrackManifest} from '../../site/tracks/src/device-local-signature.js';
import {installDeviceTrackPackage} from '../../site/tracks/src/device-package-install.js';
import {trackManifestSigningBytes} from '../../site/tracks/src/manifest.js';
import {queryInstalledDevicePackage} from '../../site/tracks/src/device-package-query.js';

class NativePackageBridge{
 constructor(){
  this.child=spawn(path.resolve('.codex-build/hostlink-codec/native/track_snapshot_owner'+(process.platform==='win32'?'.exe':'')),['package-protocol'],{stdio:['pipe','pipe','pipe']});
  this.pending=[];this.stderr='';this.calls=[];
  this.child.stderr.on('data',chunk=>{this.stderr+=chunk;});
  this.ready=new Promise((resolve,reject)=>{this.pending.push({resolve,reject});});
  this.exit=new Promise((resolve,reject)=>{this.child.on('error',reject);this.child.on('exit',code=>{if(code!==0){const error=Error(`Native bridge exit ${code}: ${this.stderr}`);for(const request of this.pending.splice(0))request.reject(error);reject(error);}else resolve();});});
  this.exit.catch(()=>{});
  createInterface({input:this.child.stdout}).on('line',line=>{
   const pending=this.pending.shift();if(!pending)return;
   const [status,hex]=line.split(' ');
   if(Number(status)!==0)pending.reject(Object.assign(Error(`Native device status ${status}`),{code:Number(status)}));
   else pending.resolve(new Uint8Array(Buffer.from(hex,'hex')));
  });
 }
 send(line){return new Promise((resolve,reject)=>{this.pending.push({resolve,reject});this.child.stdin.write(line+'\n',error=>{if(error)reject(error);});});}
 async request(op,body){this.calls.push(op);return decode(await this.send(`${op} ${Buffer.from(encode(body)).toString('hex')}`),471);}
 async finish(){await this.send('close');this.child.stdin.end();await this.exit;assert.equal(this.stderr,'');}
}
test('browser CMP v2 package -> actual ESP upload/prepare/retention -> native Ed25519 signature, same-operation reopening without resigning', {timeout:20000},async()=>{
 const bridge=new NativePackageBridge();
 try{
  const publicKey=await bridge.ready,id=value=>new Uint8Array(16).fill(value);
  const kml=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>');
  const photo=new Uint8Array(641).fill(17),photoHash=await Identity.fullHash(photo),kmlHash=await Identity.fullHash(kml);
  const index=encode([1,kmlHash,kml.length,[['photo',photoHash,photo.length,'image/png','photos/p.png',0,1],['optional',photoHash,photo.length,'image/png','photos/o.png',0,0]]]);
  const origin=await Identity.generate(),originManifest=encodeTrackManifest([1,await origin.getPublicKey(),id(21),0,1,null,0,100,100,'Original source','',[10000000,20000000,10000000,20000000],[0,null,null,1],[kmlHash,kml.length,'application/vnd.google-earth.kml+xml',1]]),originProof=await verifyTrackManifest([originManifest,await origin.sign(trackManifestSigningBytes(originManifest))]),sourceReference=[originProof.objectId,originProof.revisionHash];
  const manifest=encodeTrackManifest([2,publicKey,id(1),0,1,null,0,100,100,'Native package','',[10000000,20000000,10000000,20000000],
   [0,null,null,1],[kmlHash,kml.length,'application/vnd.google-earth.kml+xml',1],[0,[],0,0,0,0,0,sourceReference],[await Identity.fullHash(index),index.length,2,1]]);
  const input={kml,manifest,author:{verified:true,publicKey,generation:1},operationId:id(2),kmlUploadId:id(3),manifestUploadId:id(4),
   attachmentIndex:index,attachmentFiles:[{id:'photo',content:photo}],attachmentUploadIds:[{id:'photo',uploadId:id(5)}],indexUploadId:id(6),packageUploadId:id(7)};
  const options={operations:[32,33,35,99],prepareOptions:{retryMs:1,timeoutMs:15000}};
  const first=await signDeviceTrackManifest(bridge,input,options),verified=await verifyTrackManifest(first.signed,{expectedAuthor:publicKey});
  assert.ok(equal(first.revisionHash,verified.revisionHash));assert.ok(equal(first.objectId,verified.objectId));
  assert.deepEqual(verified.record[14][7],sourceReference);
  assert.deepEqual(decode(await bridge.send('state'),471),[1,1]); // Only bounded result metadata survives.
  await bridge.send('close');assert.deepEqual(decode(await bridge.send('state'),471),[1,0]);
  const reopened=await signDeviceTrackManifest(bridge,input,options);
  assert.deepEqual(reopened.signed,first.signed);assert.deepEqual(decode(await bridge.send('state'),471),[1,1]);
  assert.ok(bridge.calls.includes(99)&&bridge.calls.every(op=>[1,32,33,35,99].includes(op)));
 }finally{await bridge.finish();}
});
test('browser installs a foreign-author package through actual C++ single scheduler slot, releases large stages and never re-signs',{timeout:20000},async()=>{
 const bridge=new NativePackageBridge();
 try{
  const localPublic=await bridge.ready,remote=await Identity.generate(),remotePublic=await remote.getPublicKey(),id=value=>new Uint8Array(16).fill(value);
  assert.ok(!equal(localPublic,remotePublic));
  const kml=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>');
  const photo=new Uint8Array(641).fill(7),photoHash=await Identity.fullHash(photo),kmlHash=await Identity.fullHash(kml);
  const index=encode([1,kmlHash,kml.length,[['photo',photoHash,photo.length,'image/png','photos/p.png',0,1]]]);
  const manifest=encodeTrackManifest([2,remotePublic,id(11),0,1,null,0,100,100,'Foreign route','',[10000000,20000000,10000000,20000000],
   [0,null,null,1],[kmlHash,kml.length,'application/vnd.google-earth.kml+xml',1],[0,[],0,0,0,0,0,null],[await Identity.fullHash(index),index.length,1,1]]);
  const signed=[manifest,await remote.sign(trackManifestSigningBytes(manifest))],verified=await verifyTrackManifest(signed);
  const input={kml,signed,author:{verified:true,publicKey:localPublic,generation:1},operationId:id(12),kmlUploadId:id(13),signedUploadId:id(14),
   objectId:verified.objectId,revisionHash:verified.revisionHash,attachmentIndex:index,attachmentFiles:[{id:'photo',content:photo}],
   attachmentUploadIds:[{id:'photo',uploadId:id(15)}],indexUploadId:id(16),packageUploadId:id(17)};
  const options={operations:[32,33,35,100],retryMs:1,timeoutMs:15000};
  const installed=await installDeviceTrackPackage(bridge,input,options);
  assert.ok(installed.installed&&equal(installed.objectId,verified.objectId)&&equal(installed.revisionHash,verified.revisionHash));
  assert.deepEqual(installed.signed,signed);assert.ok(installed.target.startsWith('/routes/p-'));
  assert.deepEqual(decode(await bridge.send('state'),471),[0,1]);
  await bridge.send('close');assert.deepEqual(decode(await bridge.send('state'),471),[0,0]);
  const resumed=await installDeviceTrackPackage(bridge,input,options);assert.equal(resumed.target,installed.target);assert.deepEqual(resumed.signed,signed);
  assert.deepEqual(decode(await bridge.send('state'),471),[0,1]);
  await bridge.send('close');await bridge.send('clear-uploads');
  const query={author:input.author,operationId:id(18),objectId:verified.objectId,revisionHash:verified.revisionHash},queryOptions={operations:[101],retryMs:1,timeoutMs:15000};
  const checked=await queryInstalledDevicePackage(bridge,query,queryOptions);assert.ok(checked.installed&&!checked.missing&&checked.target===installed.target);
  await bridge.send('corrupt-cache');
  // Same query ID must reread files instead of using the old success metadata.
  await assert.rejects(queryInstalledDevicePackage(bridge,query,queryOptions),error=>error.code===8);
  await bridge.send('corrupt-cache');await bridge.send('remove-installed');
  const missing=await queryInstalledDevicePackage(bridge,query,queryOptions);assert.ok(!missing.installed&&missing.missing);
  assert.ok(bridge.calls.includes(100)&&bridge.calls.includes(101)&&bridge.calls.every(op=>[1,32,33,35,100,101].includes(op)));
 }finally{await bridge.finish();}
});
