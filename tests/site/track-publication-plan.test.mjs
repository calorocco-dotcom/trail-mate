import test from 'node:test';
import assert from 'node:assert/strict';
import {encodeTrackPublicationPlan,decodeTrackPublicationPlan,validateTrackPublicationResume} from '../../site/tracks/src/publication-plan.js';
import {encodeTrackManifest} from '../../site/tracks/src/manifest.js';
const bin=(size,value)=>new Uint8Array(size).fill(value);
function fixture(){const fileHash=bin(32,6),publicKey=bin(64,7);
 return {operationId:bin(16,1),kmlUploadId:bin(16,2),manifestUploadId:bin(16,3),generation:9,fileHash,fileBytes:100,
  manifest:encodeTrackManifest([1,publicKey,bin(16,4),0,1,null,0,100,100,'山径','',[0,0,0,0],[0,null,null,1],[fileHash,100,'application/vnd.google-earth.kml+xml',1]])};}
test('durable plan preserves exact IDs, manifest bytes and file binding',()=>{
 const plan=fixture(),raw=encodeTrackPublicationPlan(plan),restored=decodeTrackPublicationPlan(raw);
 assert.deepEqual(restored,plan);assert.ok(raw.length<=4352);raw.fill(0);assert.deepEqual(restored,plan);
 const resumed=validateTrackPublicationResume(restored,{verified:true,publicKey:bin(64,7),generation:9},bin(32,6),100);
 assert.deepEqual(resumed,plan);restored.operationId.fill(8);assert.deepEqual(resumed.operationId,plan.operationId);
});
test('changed identity, generation or draft requires a new preparation',()=>{
 const plan=fixture(),author={verified:true,publicKey:bin(64,7),generation:9};
 for(const wrong of [{...author,verified:false},{...author,generation:10},{...author,publicKey:bin(64,8)}])
  assert.throws(()=>validateTrackPublicationResume(plan,wrong,plan.fileHash,100),/身份已变化/);
 assert.throws(()=>validateTrackPublicationResume(plan,author,bin(32,8),100),/草稿已变化/);
 assert.throws(()=>validateTrackPublicationResume(plan,author,plan.fileHash,101),/草稿已变化/);
 assert.throws(()=>encodeTrackPublicationPlan({...plan,fileHash:bin(32,8)}),/绑定不一致/);
 assert.throws(()=>encodeTrackPublicationPlan({...plan,manifestUploadId:plan.kmlUploadId}),/字段无效/);
});
