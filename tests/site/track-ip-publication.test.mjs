import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {publishIPTrack} from '../../site/tracks/src/ip-publication.js';

test('IP upload resumes durable prefix and validates real signed publication receipt',async()=>{
 const author=await Identity.generate(),directory=await Identity.generate(),authorKey=await author.getPublicKey(),directoryKey=await directory.getPublicKey();
 const source=new TextEncoder().encode('<kml>'+ ' '.repeat(2100)+'</kml>'),digest=await Identity.fullHash(source),epoch=new Uint8Array(16).fill(2),requestId=new Uint8Array(16).fill(3),token=new Uint8Array(16).fill(4);
 const raw=encodeTrackManifest([1,authorKey,new Uint8Array(16).fill(7),0,1,null,0,100,100,'IP trail','',[0,0,1,1],[0,null,null,2],[digest,source.length,'application/vnd.google-earth.kml+xml',1]]);
 const signed=[raw,await author.sign(trackManifestSigningBytes(raw))],manifest=await verifyTrackManifest(signed);
 const delivery=await Identity.truncatedHash(Uint8Array.from([...((await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10)),...directory.identityHash]));
 const receiptRaw=encode([1,requestId,manifest.objectId,manifest.revisionHash,1,0,digest,source.length,directoryKey,delivery,epoch,1,100]);
 const receipt=[receiptRaw,await directory.sign(Uint8Array.from([...new TextEncoder().encode('TrailMate.Track.Receipt.v1\0'),...receiptRaw]))];
 const options={authorPublicKey:authorKey,directoryPublicKey:directoryKey,directoryEpoch:epoch,requestId};let offset=37,sideEffects=0;
 const request=async(op,body,settings)=>{
  sideEffects++;if(op===4){assert.deepEqual(body,[digest,source.length]);return[token,offset,0,1024];}
  if(op===5){assert.deepEqual(body[0],token);assert.equal(body[1],offset);assert.ok(body[2].length<=1024);assert.deepEqual(body[2],source.slice(offset,offset+body[2].length));offset+=body[2].length;return[token,offset];}
  if(op===6){assert.equal(offset,source.length);return[digest,source.length,1,0];}
  assert.equal(op,7);assert.deepEqual(settings.requestId,requestId);assert.deepEqual(body,[signed,null]);return[receipt,1];
 };
 assert.equal((await publishIPTrack(request,signed,source,options)).directoryConfirmed,true);assert.equal(sideEffects,6);
 const wrong=source.slice();wrong[0]^=1;const before=sideEffects;await assert.rejects(publishIPTrack(request,signed,wrong,options),/differs/);assert.equal(sideEffects,before);
 let publishes=0;const lost=async(op)=>{if(op===4)return[null,source.length,1,1024];publishes++;throw Error('Reply lost');};
 await assert.rejects(publishIPTrack(lost,signed,source,options),error=>error.code==='TRACK_PUBLICATION_UNCONFIRMED'&&Buffer.from(error.requestId).equals(Buffer.from(requestId)));assert.equal(publishes,1);
 const badReceipt=[receiptRaw,new Uint8Array(64)];await assert.rejects(publishIPTrack(async op=>op===4?[null,source.length,1,1024]:[badReceipt,1],signed,source,options),/签名/);
});
