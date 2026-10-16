import test from 'node:test';
import assert from 'node:assert/strict';
import {encode} from '../../site/geocaching/src/protocol.js';
import {cleanupTrackAttachmentUploads} from '../../site/tracks/src/device-attachment-cleanup.js';
import {buildDraftAttachmentIndex} from '../../site/tracks/src/draft-attachments.js';
import {buildDeviceAttachmentPlan} from '../../site/tracks/src/device-attachment-plan.js';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {saveTrackIPCheckpoint} from '../../site/tracks/src/ip-publication-journal.js';
import {syncTrackIPPublication,readTrackDeviceSyncState} from '../../site/tracks/src/device-sync-workflow.js';
const digest=async value=>new Uint8Array(await crypto.subtle.digest('SHA-256',value));
async function fixture(){
 const source=new TextEncoder().encode('<kml><Placemark><Point><coordinates>121,31</coordinates></Point></Placemark></kml>');
 const rows=[{id:'photo',reference:'photo.png',mime:'image/png',role:0,required:true,content:new Uint8Array([1,2,3])}];
 const identity=await Identity.generate(),key=await identity.getPublicKey();
 const packet=await buildDraftAttachmentIndex(source,rows),manifest=encode([2,key,new Uint8Array(16).fill(2),0,1,null,0,1,1,'Trail','',[1210000000,310000000,1210000000,310000000],[0,null,null,1],[await digest(source),source.length,'application/vnd.google-earth.kml+xml',1],[1,[],0,0,1,0,0,null],packet.assets]);
 const ids=[21,22,23].map(value=>new Uint8Array(16).fill(value));
 const snapshot={version:1,source,attachmentRows:rows,attachmentBindings:encode([1,await digest(manifest),ids[0],ids[1],[['photo',ids[2]]]])};
 const signed=[manifest,await identity.sign(trackManifestSigningBytes(manifest))],verified=await verifyTrackManifest(signed);
 const directory=await Identity.generate(),directoryKey=await directory.getPublicKey(),epoch=new Uint8Array(16).fill(26),requestId=new Uint8Array(16).fill(27);
 const nameHash=(await digest(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
 const directoryDelivery=await Identity.truncatedHash(new Uint8Array([...nameHash,...directory.identityHash]));
 const receiptRaw=encode([1,requestId,verified.objectId,verified.revisionHash,1,0,await digest(source),source.length,directoryKey,directoryDelivery,epoch,1,2]);
 const domain=new TextEncoder().encode('TrailMate.Track.Receipt.v1\0');
 const receipt=encode([receiptRaw,await directory.sign(new Uint8Array([...domain,...receiptRaw]))]);
 const record={stage:'confirmed',draftId:'test',draftVersion:1,authorKey:key,requestId,directoryKey,delivery:directoryDelivery,epoch,signature:signed[1],receipt,
  plan:{manifest,fileHash:await digest(source),fileBytes:source.length,operationId:new Uint8Array(16).fill(28),generation:7,kmlUploadId:new Uint8Array(16).fill(24),manifestUploadId:new Uint8Array(16).fill(25)}};
 const plan=await buildDeviceAttachmentPlan(manifest,source.length,{...packet,indexUploadId:ids[0],packageUploadId:ids[1],attachmentUploadIds:[{id:'photo',uploadId:ids[2]}]});
 return {record,snapshot,ids,plan,identity,verified,nameHash,options:{readDraft:async()=>snapshot}};
}
test('confirmed package cleanup removes only exact retained mapping, index and resource tuples',async()=>{
 const f=await fixture(),calls=[];
 await cleanupTrackAttachmentUploads({request:async(op,body)=>{calls.push([op,body]);return[];}},f.record,f.options);
 const sources=[[f.ids[1],10,f.plan.mapping],[f.ids[0],8,f.plan.index],[f.ids[2],9,f.snapshot.attachmentRows[0].content]];
 assert.equal(calls.length,3);
 for(let i=0;i<sources.length;i++)assert.deepEqual(calls[i],[37,[sources[i][0],sources[i][1],sources[i][2].length,await digest(sources[i][2])]]);
});
test('failed cleanup can replay exact tuples without signing or recreating IDs',async()=>{
 const f=await fixture(),calls=[];let fail=true;
 const session={request:async(op,body)=>{calls.push([op,body]);if(body[1]===8&&fail){fail=false;throw Error('busy');}return[];}};
 await assert.rejects(cleanupTrackAttachmentUploads(session,f.record,f.options));
 await cleanupTrackAttachmentUploads(session,f.record,f.options);
 assert.deepEqual(calls[0],calls[2]);assert.ok(calls.every(([op])=>op===37));
});
test('unconfirmed, changed and missing bindings cannot delete staging',async()=>{
 for(const change of [f=>{f.record.stage='signed';},f=>{f.snapshot.version++;},f=>{delete f.snapshot.attachmentBindings;},f=>{f.snapshot.source[0]^=1;},f=>{f.snapshot.attachmentBindings[4]^=1;}]){
  const f=await fixture();change(f);let calls=0;
  await assert.rejects(cleanupTrackAttachmentUploads({request:async()=>{calls++;return[];}},f.record,f.options));assert.equal(calls,0);
 }
});
test('three complete package synchronizations release every upload under the existing two-slot quota',async()=>{
 const staged=new Map();
 for(let cycle=0;cycle<3;cycle++){
  const f=await fixture(),entries=new Map(),storage={getItem:key=>entries.get(key)??null,setItem:(key,value)=>entries.set(key,value)};
  const author={verified:true,publicKey:f.record.authorKey,generation:7};
  const put=async(id,kind,content)=>{
   assert.ok([...staged.values()].filter(row=>row.kind===kind).length<2,'retained uploads exhausted the normal quota');
   staged.set(id.toString(),{kind,total:content.length,hash:await digest(content)});
  };
  await put(f.record.plan.kmlUploadId,3,f.snapshot.source);
  await put(f.record.plan.manifestUploadId,4,f.record.plan.manifest);
  await put(f.ids[0],8,f.plan.index);await put(f.ids[1],10,f.plan.mapping);await put(f.ids[2],9,f.snapshot.attachmentRows[0].content);
  const delivery=await Identity.truncatedHash(new Uint8Array([...f.nameHash,...f.identity.identityHash]));
  let confirmed=false,confirmations=0,failMapping=cycle===1;
  const session={request:async(op,body)=>{
   if(op===1)return[f.record.authorKey,f.identity.identityHash,delivery,7];
   if(op===32){await put(body[0],7,f.record.receipt);return[body[0],320,body[2],1];}
   if(op===97){confirmed=true;confirmations++;return[body[0],body[1],7,1,f.verified.objectId,f.verified.revisionHash,1,1];}
   assert.equal(op,37);assert.ok(confirmed,'cleanup preceded device confirmation');
   if(body[1]===10&&failMapping){failMapping=false;throw Error('Mapping cleanup interrupted');}
   const row=staged.get(body[0].toString());assert.deepEqual(body.slice(1),[row.kind,row.total,row.hash]);staged.delete(body[0].toString());return[];
  }};
  saveTrackIPCheckpoint(f.record,{storage});
  const options={...f.options,storage,operations:[32,33,35,37,97],locks:{request:async(_name,_options,work)=>work({})}};
  if(cycle===1){
   await assert.rejects(syncTrackIPPublication(session,f.record,author,options));
   const pending=await readTrackDeviceSyncState(f.record,options);assert.equal(pending.stage,1);assert.equal(pending.cleaned,7);
  }
  await syncTrackIPPublication(session,f.record,author,options);assert.equal(confirmations,1);
  assert.equal((await readTrackDeviceSyncState(f.record,options)).stage,2);
  assert.equal(staged.size,0,'a completed package left attachment staging behind');
 }
});
