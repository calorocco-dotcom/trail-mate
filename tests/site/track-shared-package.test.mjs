import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {trackId} from '../../site/tracks/src/shared-id.js';
import {readSharedTrackPackage} from '../../site/tracks/src/shared-track-package.js';
import {attachmentImageInfo} from '../../site/tracks/src/attachment-image-info.js';
const hex=bytes=>Buffer.from(bytes).toString('hex');
const identity=await Identity.generate(),author=await identity.getPublicKey(),source=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><description><![CDATA[<img src="photos/a.jpg">]]></description><LineString><coordinates>0,0 1,1</coordinates></LineString></Placemark></kml>');
const photo=new Uint8Array(await readFile('tests/hostlink/fixtures/png-cache/red-16x8.jpg')),notes=new TextEncoder().encode('notes'),hash=await Identity.fullHash(source),photoHash=await Identity.fullHash(photo),notesHash=await Identity.fullHash(notes);
async function fixture({duplicate=false,indexMissing=false,requiredMissing=false,optionalMissing=true,change}={}){
 const entries=[['photo',photoHash,photo.length,'image/jpeg','photos/a.jpg',0,1],['notes',notesHash,notes.length,'text/plain','notes.txt',3,0]];
 if(duplicate)entries.push(['icon',photoHash,photo.length,'image/jpeg','icon.jpg',1,0]);
 const index=encode([1,hash,source.length,entries]),indexHash=await Identity.fullHash(index);
 const raw=encodeTrackManifest([2,author,new Uint8Array(16).fill(7),0,1,null,0,100,100,'Package','',[0,0,10000000,10000000],[0,null,null,2],[hash,source.length,'application/vnd.google-earth.kml+xml',1],[0,[],0,1,0,0,0,null],[indexHash,index.length,entries.length,1]]),signed=[raw,await identity.sign(trackManifestSigningBytes(raw))],verified=await verifyTrackManifest(signed);
 const files=new Map([[hex(hash),source],[hex(indexHash),index],[hex(photoHash),photo],[hex(notesHash),notes]]),calls=[];let heads=0;
 const request=async(op,body)=>{
  calls.push([op,body]);let result;
  if(op===2)result=[signed,++heads===1?1:0];
  else if(op===14)result=[1,verified.revisionHash,1,indexMissing?0:1,indexMissing||requiredMissing?0:1,indexMissing?[]:entries.map(row=>[row[0],row[1],row[2],row[6],row[0]==='photo'&&requiredMissing||row[0]==='notes'&&optionalMissing?0:1])];
  else{assert.equal(op,8);const content=files.get(hex(body[0]));assert.ok(content);result=[body[0],content.length,body[1],content.slice(body[1],body[1]+body[2])];}
  return change?.(op,result,body)??result;
 };
 return {request,calls,verified,index,indexHash};
}
test('original signature/index/files are verified, optional absence stays explicit and currentness is refreshed',async()=>{
 const f=await fixture({duplicate:true}),progress=[];
 const packet=await readSharedTrackPackage(f.request,trackId(f.verified.objectId),{packageSupported:true,chunkBytes:37,onProgress:row=>progress.push(row)});
 assert.equal(packet.ready,true);assert.equal(packet.allComplete,false);assert.equal(packet.detail.current,false);assert.deepEqual(packet.detail.source,source);assert.deepEqual(packet.attachmentIndex,f.index);assert.deepEqual(packet.files.map(row=>row.id),['photo','icon']);assert.deepEqual(packet.files[0].content,photo);assert.equal(packet.files[0].content,packet.files[1].content);
 assert.equal(packet.rows[1].verified,false);assert.equal(packet.rows[1].required,false);
 assert.equal(f.calls.filter(([op,body])=>op===8&&hex(body[0])===hex(photoHash)&&body[1]===0).length,1);assert.ok(progress.some(row=>row.reference==='photos/a.jpg'));assert.equal(f.calls.at(-1)[0],2);
});
test('missing index and mandatory files do not produce a ready package',async()=>{
 for(const options of [{indexMissing:true},{requiredMissing:true}]){
  const f=await fixture(options),packet=await readSharedTrackPackage(f.request,trackId(f.verified.objectId),{packageSupported:true});assert.equal(packet.ready,false);
  if(options.indexMissing){assert.equal(packet.attachmentIndex,null);assert.equal(packet.files.length,0);assert.equal(f.calls.some(([op,body])=>op===8&&hex(body[0])===hex(f.indexHash)),false);}
  else{assert.equal(packet.rows[0].verified,false);assert.equal(f.calls.some(([op,body])=>op===8&&hex(body[0])===hex(photoHash)),false);}
 }
});
test('unadvertised package support fails before file traffic',async()=>{
 const f=await fixture();await assert.rejects(readSharedTrackPackage(f.request,trackId(f.verified.objectId)),error=>error.code==='TRACK_PACKAGE_UNSUPPORTED');assert.deepEqual(f.calls.map(row=>row[0]),[2]);
});
test('bad index, optional corruption and malformed chunks cannot become absence',async()=>{
 for(const target of ['index','optional']){
  const f=await fixture({optionalMissing:false,change:(op,row,body)=>{if(op===8&&hex(body[0])===hex(target==='optional'?notesHash:f.indexHash))row[3][0]^=1;return row;}});
  await assert.rejects(readSharedTrackPackage(f.request,trackId(f.verified.objectId),{packageSupported:true}),error=>error.code==='TRACK_PACKAGE_HASH');
 }
 const f=await fixture({change:(op,row)=>{if(op===8)row[2]++;return row;}});await assert.rejects(readSharedTrackPackage(f.request,trackId(f.verified.objectId),{packageSupported:true}),error=>error.code==='TRACK_PACKAGE_FILE_RESPONSE');
});
test('invalid availability and cancellation reject before a partial result escapes',async()=>{
 const f=await fixture({change:(op,row)=>{if(op===14)row[5][0][0]='other';return row;}});await assert.rejects(readSharedTrackPackage(f.request,trackId(f.verified.objectId),{packageSupported:true}),/package status/);
 const g=await fixture(),abort=new AbortController();await assert.rejects(readSharedTrackPackage(g.request,trackId(g.verified.objectId),{packageSupported:true,signal:abort.signal,onProgress:event=>{if(event.phase==='index')abort.abort();}}),error=>error.name==='AbortError');assert.equal(g.calls.some(([op,body])=>op===8&&hex(body[0])===hex(photoHash)),false);
});
test('image admission reads real dimensions and refuses oversized or malformed headers',()=>{
 assert.deepEqual(attachmentImageInfo(photo),{width:16,height:8,mime:'image/jpeg'});
 const png=new Uint8Array(33);png.set([137,80,78,71,13,10,26,10]);png[11]=13;png.set(new TextEncoder().encode('IHDR'),12);const view=new DataView(png.buffer);view.setUint32(16,320);view.setUint32(20,180);assert.deepEqual(attachmentImageInfo(png),{width:320,height:180,mime:'image/png'});view.setUint32(16,9000);assert.equal(attachmentImageInfo(png),null);assert.equal(attachmentImageInfo(new Uint8Array([255,216,255,192,0,1])),null);
});
