import test from 'node:test';
import assert from 'node:assert/strict';
import {waitTrackJob} from '../../site/tracks/src/wait-track-job.js';
import {cancelTrackJob} from '../../site/tracks/src/cancel-job.js';
import {readTrackJob} from '../../site/tracks/src/device-job.js';
const bin=(n,v)=>new Uint8Array(n).fill(v);
const binding=()=>({operationId:bin(16,1),objectId:bin(32,2),revisionHash:bin(32,3),generation:7,revision:2,directory:bin(16,4),directoryEpoch:bin(16,5),fileHash:bin(32,6),fileBytes:100,authorHash:bin(32,7)});

test('pausing an in-flight query reaches the transport without sending cancel',async()=>{
 const controller=new AbortController(),calls=[];
 const session={request(operation,body,{signal}){calls.push(operation);assert.equal(signal,controller.signal);controller.abort();return Promise.reject(signal.reason);}};
 await assert.rejects(waitTrackJob(session,binding(),{signal:controller.signal}),error=>error.code==='JOB_WAIT_PAUSED'&&error.operationId[0]===1);
 assert.deepEqual(calls,[48]);
});
test('pausing an already-sent cancel retains an unresolved operation',async()=>{
 const controller=new AbortController(),calls=[];
 const session={request(operation,body,{signal}){calls.push(operation);assert.equal(signal,controller.signal);controller.abort();return Promise.reject(signal.reason);}};
 await assert.rejects(cancelTrackJob(session,binding(),{signal:controller.signal}),error=>error.code==='JOB_CANCEL_PENDING'&&error.operationId[0]===1);
 assert.deepEqual(calls,[50]);
});
test('a late transport response is rejected after cancellation',async()=>{
 const b=binding(),controller=new AbortController();
 await assert.rejects(readTrackJob({async request(){controller.abort();return [b.operationId,b.objectId,b.revisionHash,7,2,b.directory,b.directoryEpoch,b.fileHash,100,100,4,2,b.authorHash,0];}},b,{signal:controller.signal}),error=>error.name==='AbortError');
});
test('an already-aborted query sends no USB traffic',async()=>{
 const controller=new AbortController();controller.abort();
 await assert.rejects(readTrackJob({request(){assert.fail('unexpected USB');}},binding(),{signal:controller.signal}),error=>error.name==='AbortError');
});
