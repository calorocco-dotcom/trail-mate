import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {signDeviceTrackManifest} from '../../site/tracks/src/device-local-signature.js';
test('local track signing uses no device directory or RF operations and verifies actual signature',async()=>{
 const identity=await Identity.generate(),pub=await identity.getPublicKey(),kml=new TextEncoder().encode('<kml/>'),digest=await Identity.fullHash(kml);
 const manifest=encodeTrackManifest([1,pub,new Uint8Array(16).fill(2),0,1,null,0,100,100,'Local trail','',[0,0,0,0],[0,null,null,1],[digest,kml.length,'application/vnd.google-earth.kml+xml',1]]);
 const signature=await identity.sign(trackManifestSigningBytes(manifest)),verified=await verifyTrackManifest([manifest,signature]),delivery=await Identity.truncatedHash(Uint8Array.from([...((await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10)),...identity.identityHash]));
 const input={kml,manifest,author:{verified:true,publicKey:pub,generation:7},operationId:new Uint8Array(16).fill(3),kmlUploadId:new Uint8Array(16).fill(4),manifestUploadId:new Uint8Array(16).fill(5)};
 const calls=[];let upload,bad=false;
 const session={request:async(op,body)=>{calls.push(op);if(op===1)return[pub,identity.identityHash,delivery,7];if(op===32){upload={id:body[0],total:body[2],offset:0};return[upload.id,320,0,0];}
  if(op===33){upload.offset+=body[2].length;return[upload.id,320,upload.offset,0];}if(op===35)return[upload.id,320,upload.total,1];
  assert.equal(op,96);return[body[0],1,verified.objectId,verified.revisionHash,1,bad?new Uint8Array(64):signature];}};
 await assert.rejects(signDeviceTrackManifest(session,input,{operations:[32,33,35]}));assert.equal(calls.length,0);
 const result=await signDeviceTrackManifest(session,input,{operations:[32,33,35,96]});assert.deepEqual(result.signed,[manifest,signature]);assert.ok(calls.every(op=>[1,32,33,35,96].includes(op)));
 bad=true;await assert.rejects(signDeviceTrackManifest(session,input,{operations:[32,33,35,96]}),/签名/);
});
