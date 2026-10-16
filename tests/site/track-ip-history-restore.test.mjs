import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {parseTrackReference} from '../../site/tracks/src/shared-id.js';
import {restoreTrackIPHistoryFile} from '../../site/tracks/src/ip-history-restore.js';
const bin=(size,value)=>new Uint8Array(size).fill(value);
test('history restore pins exact signed version and rejects changed KML or directory',async()=>{
 const author=await Identity.generate(),directory=await Identity.generate(),pub=await author.getPublicKey(),source=new TextEncoder().encode('<kml/>'),fileHash=await Identity.fullHash(source),destination=bin(16,2),epoch=bin(16,3);
 const raw=encodeTrackManifest([1,pub,bin(16,4),0,1,null,0,100,100,'Historical trail','',[0,0,0,0],[0,null,null,1],[fileHash,source.length,'application/vnd.google-earth.kml+xml',1]]),signature=await author.sign(trackManifestSigningBytes(raw)),verified=await verifyTrackManifest([raw,signature]);
 const record={stage:'signed',draftId:'history',draftVersion:1,authorKey:pub,requestId:bin(16,5),directoryKey:await directory.getPublicKey(),delivery:destination,epoch,
  plan:{operationId:bin(16,6),kmlUploadId:bin(16,7),manifestUploadId:bin(16,8),generation:7,manifest:raw,fileHash,fileBytes:source.length},signature,receipt:null};
 let calls=0,bad=false;const entry={ready:true,epoch,identity:directory},key=Buffer.from(destination).toString('hex');
 const network={directories:new Map([[key,entry]]),readTrack:async(target,reference)=>{calls++;assert.equal(target,key);const parsed=parseTrackReference(reference);assert.deepEqual(parsed.id,verified.objectId);assert.deepEqual(parsed.revision,verified.revisionHash);return{manifest:raw,signature,source:bad?new Uint8Array(source.length):source};}};
 const options={baseUrl:'https://example.test/trail-mate/'};assert.deepEqual((await restoreTrackIPHistoryFile(record,network,options)).source,source);
 bad=true;await assert.rejects(restoreTrackIPHistoryFile(record,network,options),/hash differs/);entry.epoch=bin(16,9);await assert.rejects(restoreTrackIPHistoryFile(record,network,options),/directory changed/);assert.equal(calls,2);
});
