import test from 'node:test';import assert from 'node:assert/strict';
import {encodeTrackJobBinding,decodeTrackJobBinding} from '../../site/tracks/src/job-binding.js';
const bin=(n,v)=>new Uint8Array(n).fill(v);
const binding=()=>({operationId:bin(16,1),objectId:bin(32,2),revisionHash:bin(32,3),generation:7,revision:2,
 directory:bin(16,4),directoryEpoch:bin(16,5),authorHash:bin(32,6),fileHash:bin(32,7),fileBytes:100});
test('task binding round trip retains exact operation, identity, directory and file',()=>{
 const b=binding(),raw=encodeTrackJobBinding(b),restored=decodeTrackJobBinding(raw);
 assert.deepEqual(restored,b);raw.fill(0);assert.equal(restored.operationId[0],1);assert.equal(restored.directoryEpoch[0],5);
});
test('invalid identity, directory, file and version cannot form a retained task',()=>{
 for(const mutate of [b=>b.operationId=bin(16,0),b=>b.generation=0,b=>b.revision=0,b=>b.directoryEpoch=bin(16,0),
   b=>b.authorHash=bin(32,0),b=>b.fileHash=bin(32,0),b=>b.fileBytes=8388609]){
   const b=binding();mutate(b);assert.throws(()=>encodeTrackJobBinding(b));
 }
 const raw=encodeTrackJobBinding(binding());assert.throws(()=>decodeTrackJobBinding(raw.slice(0,-1)));
 assert.throws(()=>decodeTrackJobBinding(new Uint8Array(513)));
});
