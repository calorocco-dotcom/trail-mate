import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {trackId,trackShareLink} from '../../site/tracks/src/shared-id.js';
import {readSharedTrack} from '../../site/tracks/src/shared-track-read.js';

const identity=await Identity.generate(),author=await identity.getPublicKey();
const source=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>120,30 121,31</coordinates></LineString></Placemark></kml>');
const digest=await Identity.fullHash(source);
const record=[1,author,new Uint8Array(16).fill(7),0,1,null,0,100,100,'Shared trail','',[1200000000,300000000,1210000000,310000000],[0,null,null,2],[digest,source.length,'application/vnd.google-earth.kml+xml',1]];
const raw=encodeTrackManifest(record),signed=[raw,await identity.sign(trackManifestSigningBytes(raw))],manifest=await verifyTrackManifest(signed);
function transport(calls,change){return async(operation,body)=>{calls.push([operation,body]);if(operation===2)return change?.(operation,[signed,1])??[signed,1];assert.equal(operation,8);assert.deepEqual(body[0],digest);const response=[digest,source.length,body[1],source.slice(body[1],body[1]+body[2])];return change?.(operation,response)??response;};}

test('direct ID and pinned share link bind real author signature and exact KML bytes',async()=>{
 for(const reference of [trackId(manifest.objectId),trackShareLink('https://example.github.io/trail-mate/',manifest.objectId,{revision:manifest.revisionHash})]){
   const calls=[],progress=[];const result=await readSharedTrack(transport(calls),reference,{chunkBytes:37,onProgress:event=>progress.push(event)});
   assert.deepEqual(result.source,source);assert.deepEqual(result.objectId,manifest.objectId);assert.equal(result.current,true);assert.equal(calls[0][0],2);assert.deepEqual(calls.slice(1).map(([,body])=>body[1]),[0,37,74,111]);assert.equal(progress.at(-1).phase,'verified');
 }
});
test('wrong object, pinned revision and signature are rejected before file requests',async()=>{
 for(const reference of [trackId(new Uint8Array(32).fill(9)),trackShareLink('https://example.test/',manifest.objectId,{revision:new Uint8Array(32).fill(9)})]){const calls=[];await assert.rejects(readSharedTrack(transport(calls),reference),error=>error.code==='TRACK_REFERENCE_MISMATCH');assert.equal(calls.length,1);}
 const signature=signed[1].slice();signature[0]^=1;const calls=[];await assert.rejects(readSharedTrack(transport(calls,(op)=>op===2?[[raw,signature],1]:undefined),trackId(manifest.objectId)),error=>error.code==='TRACK_MANIFEST_INVALID');assert.equal(calls.length,1);
});
test('file size, offset, digest and bytes are validated independently of author signature',async()=>{
 for(const change of [row=>{row[1]++;},row=>{row[2]++;},row=>{row[0]=new Uint8Array(32);},row=>{row[3]=row[3].slice(1);}])await assert.rejects(readSharedTrack(transport([], (op,row)=>{if(op===8)change(row);return row;}),trackId(manifest.objectId)),error=>error.code==='TRACK_FILE_RESPONSE_INVALID');
 await assert.rejects(readSharedTrack(transport([],(op,row)=>{if(op===8)row[3][0]^=1;return row;}),trackId(manifest.objectId)),error=>error.code==='TRACK_FILE_HASH_MISMATCH');
 const calls=[];await assert.rejects(readSharedTrack(transport(calls),trackId(manifest.objectId),{maxBytes:source.length-1}),error=>error.code==='TRACK_FILE_TOO_LARGE');assert.equal(calls.length,1);
});
test('cancellation and metadata-only reads never fetch unexpected chunks or report verification',async()=>{
 const calls=[];const metadata=await readSharedTrack(transport(calls),trackId(manifest.objectId),{download:false});assert.equal(metadata.source,undefined);assert.equal(calls.length,1);
 const abort=new AbortController(),cancelledCalls=[],progress=[];
 await assert.rejects(readSharedTrack(transport(cancelledCalls),trackId(manifest.objectId),{signal:abort.signal,chunkBytes:37,onProgress:event=>{progress.push(event);if(event.offset)abort.abort();}}),error=>error.name==='AbortError');assert.equal(cancelledCalls.length,2);assert.equal(progress.some(event=>event.phase==='verified'),false);
 const lateAbort=new AbortController(),lateCalls=[];await assert.rejects(readSharedTrack(async(op,body)=>{const value=await transport(lateCalls)(op,body);lateAbort.abort();return value;},trackId(manifest.objectId),{signal:lateAbort.signal}),error=>error.name==='AbortError');assert.equal(lateCalls.length,1);
});
