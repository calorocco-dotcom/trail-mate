import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode,equal} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {signDeviceTrackManifest} from '../../site/tracks/src/device-local-signature.js';
import {prepareTrackPublication} from '../../site/tracks/src/device-publication-prepare.js';

const id=value=>new Uint8Array(16).fill(value);
async function fixture(assets=true){
 const identity=await Identity.generate(),pub=await identity.getPublicKey(),kml=new TextEncoder().encode('<kml/>'),kmlHash=await Identity.fullHash(kml);
 const resource=new Uint8Array(641).fill(7),resourceHash=await Identity.fullHash(resource);
 const index=encode([1,kmlHash,kml.length,[['photo',resourceHash,resource.length,'image/png','photos/p.png',0,1],['optional',resourceHash,resource.length,'image/png','photos/o.png',0,0]]]);
 const manifest=encodeTrackManifest([2,pub,id(1),0,1,null,0,100,100,'Package','',[0,0,0,0],[0,null,null,1],[kmlHash,kml.length,'application/vnd.google-earth.kml+xml',1],
  [0,[],0,0,0,0,0,null],assets?[await Identity.fullHash(index),index.length,2,1]:null]);
 const signature=await identity.sign(trackManifestSigningBytes(manifest)),verified=await verifyTrackManifest([manifest,signature]);
 const input={kml,manifest,author:{verified:true,publicKey:pub,generation:7},operationId:id(2),kmlUploadId:id(3),manifestUploadId:id(4),
  ...(assets?{attachmentIndex:index,attachmentFiles:[{id:'photo',content:resource}],attachmentUploadIds:[{id:'photo',uploadId:id(5)}],indexUploadId:id(6),packageUploadId:id(7)}:{})};
 const delivery=await Identity.truncatedHash(Uint8Array.from([...((await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10)),...identity.identityHash]));
 const calls=[],uploads=[],requests=[];let current,prepareCount=0;
 const session={async request(op,body){
  calls.push(op);
  if(op===1)return[pub,identity.identityHash,delivery,7];
  if(op===32){current={id:body[0].slice(),kind:body[1],total:body[2],hash:body[3].slice(),bytes:[]};uploads.push(current);return[current.id,320,0,0];}
  if(op===33){assert.equal(body[1],current.bytes.length);current.bytes.push(...body[2]);return[current.id,320,current.bytes.length,0];}
  if(op===35){assert.equal(current.bytes.length,current.total);assert.ok(equal(await Identity.fullHash(new Uint8Array(current.bytes)),current.hash));return[current.id,320,current.total,1];}
  assert.equal(op,99);requests.push(body.map(value=>value?.slice?.()??value));
  if(prepareCount++===0)throw Object.assign(Error('busy'),{code:5});
  if(prepareCount===2)return[body[0],0,null,null,0,null];
  return[body[0],1,verified.objectId,verified.revisionHash,1,signature];
 }};
 return{input,session,calls,uploads,requests,signature,verified};
}
const options={operations:[32,33,35,99],prepareOptions:{retryMs:1,timeoutMs:10000},onProgress:()=>{}};
test('v2 USB package signing freezes inputs, uploads bounded mapping last, retries same bindings and verifies actual signature',async()=>{
 const f=await fixture(),originalIndex=f.input.attachmentIndex.slice(),originalResource=f.input.attachmentFiles[0].content.slice(),originalId=f.input.attachmentUploadIds[0].uploadId.slice();
 const supported=options.operations.slice(),request=f.session.request.bind(f.session);
 f.session.request=async(op,body)=>{if(op===1){f.input.attachmentIndex.fill(0);f.input.attachmentFiles[0].content.fill(0);f.input.attachmentUploadIds[0].uploadId.fill(0);supported.length=0;}return request(op,body);};
 const result=await signDeviceTrackManifest(f.session,f.input,{...options,operations:supported});
 assert.deepEqual(result.signed,[f.input.manifest,f.signature]);assert.ok(equal(result.revisionHash,f.verified.revisionHash));
 assert.deepEqual(f.uploads.map(row=>row.kind),[3,4,8,9,10]);
 assert.deepEqual(new Uint8Array(f.uploads[2].bytes),originalIndex);assert.deepEqual(new Uint8Array(f.uploads[3].bytes),originalResource);
 const [version,indexId,packed]=decode(new Uint8Array(f.uploads[4].bytes),1100);assert.equal(version,1);assert.deepEqual(indexId,id(6));
 assert.equal(packed.length,32);assert.deepEqual(packed.slice(0,16),originalId);assert.ok(packed.slice(16).every(value=>value===0));
 assert.ok(f.requests.length===3&&f.requests.every(body=>body.length===6&&equal(body[5],id(7))));
 assert.deepEqual(f.requests[0],f.requests[1]);assert.deepEqual(f.requests[1],f.requests[2]);assert.ok(f.calls.every(op=>[1,32,33,35,99].includes(op)));
});
test('v2 requires explicit package capability; invalid resources and missing required mappings perform zero uploads',async()=>{
 for(const fault of ['capability','resource','required','index','binding']){
  const f=await fixture();let opts=options;
  if(fault==='capability')opts={...options,operations:[32,33,35,96]};
  if(fault==='resource')f.input.attachmentFiles[0].content[0]^=1;
  if(fault==='required')f.input.attachmentUploadIds=[];
  if(fault==='index')f.input.attachmentIndex[0]^=1;
  if(fault==='binding')f.input.packageUploadId=new Uint8Array(16);
  await assert.rejects(signDeviceTrackManifest(f.session,f.input,opts));assert.equal(f.uploads.length,0);assert.equal(f.requests.length,0);
  if(fault==='capability')assert.equal(f.calls.length,0);
 }
});
test('v2 nil assets still uses domain v2 and explicit package operation with nil mapping',async()=>{
 const f=await fixture(false),result=await signDeviceTrackManifest(f.session,f.input,options);
 assert.deepEqual(f.uploads.map(row=>row.kind),[3,4]);assert.ok(f.requests.every(body=>body[5]===null));
 assert.ok(equal(result.revisionHash,f.verified.revisionHash));assert.deepEqual(result.signed,[f.input.manifest,f.signature]);
});
test('existing sealed resource IDs can be reused without resending resource bytes; required validation remains on device',async()=>{
 const f=await fixture();f.input.attachmentFiles=[];
 await signDeviceTrackManifest(f.session,f.input,options);assert.deepEqual(f.uploads.map(row=>row.kind),[3,4,8,10]);
 const failing=await fixture();failing.input.attachmentFiles=[];const request=failing.session.request.bind(failing.session);
 failing.session.request=(op,body)=>op===99?Promise.reject(Object.assign(Error('invalid sealed binding'),{code:3})):request(op,body);
 await assert.rejects(signDeviceTrackManifest(failing.session,failing.input,options),/invalid sealed binding/);
});
test('package preflight rejects bad resources before KML or manifest transfers even through direct prepare API',async()=>{
 const f=await fixture();f.input.attachmentFiles[0].content[0]^=1;
 await assert.rejects(prepareTrackPublication(f.session,f.input,options));assert.equal(f.calls.length,0);
});
