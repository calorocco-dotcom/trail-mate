import test from 'node:test';import assert from 'node:assert/strict';
import {encode} from '../../site/geocaching/src/protocol.js';
import {validatePreparedPlanBinding} from '../../site/tracks/src/prepared-plan-binding.js';
import {commitTrack} from '../../site/tracks/src/device-commit.js';
const bin=(n,v)=>new Uint8Array(n).fill(v);
async function fixture(){
 const author={verified:true,publicKey:bin(64,6),generation:7},fileHash=bin(32,8),record=[1,author.publicKey,bin(16,2),0,1,null,0,100,100,'Track','',[0,0,100,100],[0,null,null,2],[fileHash,100,'application/vnd.google-earth.kml+xml',1]],manifest=encode(record);
 const hash=async(domain,...parts)=>{const prefix=new TextEncoder().encode(domain+'\0');return new Uint8Array(await crypto.subtle.digest('SHA-256',Uint8Array.from([...prefix,...parts.flatMap(value=>[...value])])));};
 const plan={operationId:bin(16,1),kmlUploadId:bin(16,3),manifestUploadId:bin(16,4),generation:7,manifest,fileHash,fileBytes:100};
 const prepared={prepared:true,operationId:plan.operationId.slice(),generation:7,revision:1,objectId:await hash('TrailMate.Track.ID.v1',author.publicKey,record[2]),revisionHash:await hash('TrailMate.Track.Revision.v1',manifest)};
 return {plan,prepared,author};
}
test('prepared object, revision and operation match the exact persisted plan',async()=>{
 const f=await fixture();const result=await validatePreparedPlanBinding(f.plan,f.prepared,f.author,f.plan.fileHash,100);assert.deepEqual(result.manifest,f.plan.manifest);
 for(const key of ['operationId','objectId','revisionHash']){const prepared={...f.prepared,[key]:f.prepared[key].slice()};prepared[key][0]^=1;await assert.rejects(validatePreparedPlanBinding(f.plan,prepared,f.author,f.plan.fileHash,100));}
 await assert.rejects(validatePreparedPlanBinding(f.plan,{...f.prepared,revision:2},f.author,f.plan.fileHash,100));
});
test('changed device or draft cannot attach an old prepared result',async()=>{
 const f=await fixture();await assert.rejects(validatePreparedPlanBinding(f.plan,f.prepared,{...f.author,publicKey:bin(64,9)},f.plan.fileHash,100));await assert.rejects(validatePreparedPlanBinding(f.plan,f.prepared,f.author,bin(32,9),100));
});
test('pause after submitting preserves unresolved operation; pause before submits nothing',async()=>{
 const f=await fixture(),directory={capabilitiesVerified:true,protocol:'trailmate.track.v1',delivery:bin(16,4),epoch:bin(16,5)},controller=new AbortController();let calls=0;
 await assert.rejects(commitTrack({async request(op,body,{signal}){calls++;assert.equal(signal,controller.signal);controller.abort();throw signal.reason;}},f.prepared,directory,f.author,{signal:controller.signal}),error=>error.code==='COMMIT_PENDING'&&error.operationId[0]===1);
 await assert.rejects(commitTrack({async request(){calls++;}},f.prepared,directory,f.author,{signal:controller.signal}),error=>error.name==='AbortError');assert.equal(calls,1);
});
