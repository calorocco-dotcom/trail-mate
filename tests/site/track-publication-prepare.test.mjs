import test from 'node:test';
import assert from 'node:assert/strict';
import {prepareTrackPublication} from '../../site/tracks/src/device-publication-prepare.js';
import {encodeTrackManifest} from '../../site/tracks/src/manifest.js';
const bin=(size,value)=>new Uint8Array(size).fill(value);
const hash=async value=>new Uint8Array(await crypto.subtle.digest('SHA-256',value));
async function fixture(){
 const kml=new TextEncoder().encode('<kml/>'),key=bin(64,1),nonce=bin(16,2);
 const manifest=encodeTrackManifest([1,key,nonce,0,1,null,0,100,100,'山径','',[0,0,0,0],[0,null,null,1],[await hash(kml),kml.length,'application/vnd.google-earth.kml+xml',1]]);
 return {kml,manifest,author:{verified:true,publicKey:key,generation:7},operationId:bin(16,3),kmlUploadId:bin(16,4),manifestUploadId:bin(16,5)};
}
test('preflights exact file and author before serial traffic',async()=>{
 const f=await fixture(),session={async request(){assert.fail('unexpected traffic');}};
 await assert.rejects(prepareTrackPublication(session,{...f,author:{...f.author,publicKey:bin(64,8)}}),/身份/);
 await assert.rejects(prepareTrackPublication(session,{...f,kml:bin(f.kml.length,8)}),/摘要/);
 const controller=new AbortController();controller.abort();
 await assert.rejects(prepareTrackPublication(session,f,{signal:controller.signal}),{name:'AbortError'});
});
test('sequentially seals both uploads then requests signature with domain hashes',async()=>{
 const f=await fixture(),phases=[],kinds=[];let current;
 const session={async request(op,body){
  if(op===32){current={id:body[0].slice(),total:body[2],offset:0};kinds.push(body[1]);return [current.id,320,0,0];}
  if(op===33){assert.equal(body[1],current.offset);current.offset+=body[2].length;return [current.id,320,current.offset,0];}
  if(op===35)return [current.id,320,current.total,1];
  assert.equal(op,82);assert.deepEqual(body,[f.operationId,f.kmlUploadId,f.manifestUploadId,7,null]);
  const join=(domain,...parts)=>new Uint8Array(Buffer.concat([Buffer.from(domain+'\0'),...parts.map(p=>Buffer.from(p))]));
  return [body[0],1,await hash(join('TrailMate.Track.ID.v1',f.author.publicKey,bin(16,2))),await hash(join('TrailMate.Track.Revision.v1',f.manifest)),1];
 }};
 const result=await prepareTrackPublication(session,f,{onProgress:p=>phases.push(p.phase)});
 assert.deepEqual(kinds,[3,4]);assert.equal(result.prepared,true);assert.equal(result.published,false);
 assert.ok(phases.includes('uploading-kml')&&phases.includes('uploading-manifest'));assert.equal(phases.at(-1),'prepared');
});
