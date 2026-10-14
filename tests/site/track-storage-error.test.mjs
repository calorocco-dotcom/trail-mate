import test from 'node:test';
import assert from 'node:assert/strict';
import {trackDirectoryError,TRACK_STORAGE_UNAVAILABLE} from '../../site/tracks/src/directory-error.js';
import {TrackDirectoryClient} from '../../site/tracks/src/protocol-client.js';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';
import {createIPTrackReadSession} from '../../site/tracks/src/ip-read-session.js';
import {trackStorageErrorText} from '../../site/i18n/track-storage-labels.js';
import {geocachingIpPublicationText} from '../../site/i18n/geocaching-ip-publication-labels.js';
import {locales} from '../../site/i18n/registry.js';

test('only exact authenticated storage status is converted; other failures retain their meaning',()=>{
 const error=Object.assign(Error('private diagnostic'),{code:'DIRECTORY_REJECTED',status:503,reason:'storage unavailable'}),mapped=trackDirectoryError(error);
 assert.equal(mapped.code,TRACK_STORAGE_UNAVAILABLE);assert.equal(mapped.status,503);assert.equal(mapped.cause,error);assert.ok(!mapped.message.includes('private'));assert.equal(trackDirectoryError(mapped),mapped);
 for(const failure of [Error('timeout'),Object.assign(Error(),{status:503,reason:'temporarily_unavailable'}),Object.assign(Error(),{status:422,reason:'storage unavailable'}),Object.assign(Error(),{reason:'storage unavailable'})])assert.equal(trackDirectoryError(failure),failure);
});

test('production track client retains arguments and maps the superclass correlated response',async t=>{
 const directory={},body=[new Uint8Array(32)],options={requestId:new Uint8Array(16).fill(3)},calls=[];
 t.mock.method(DirectoryClient.prototype,'request',async function(...args){calls.push(args);throw Object.assign(Error('503'),{status:503,reason:'storage unavailable'});});
 const client=new TrackDirectoryClient();
 await assert.rejects(client.request(directory,4,body,options),{code:TRACK_STORAGE_UNAVAILABLE,status:503});
 assert.deepEqual(calls,[[directory,4,body,options]]);
});

test('the read adapter preserves directory binding and maps storage status without issuing retries or writes',async()=>{
 const key=new Uint8Array(64).fill(1),delivery=new Uint8Array(16).fill(2),epoch=new Uint8Array(16).fill(3),entry={ready:true,identity:{getPublicKey:async()=>key},destination:delivery,epoch,operations:[14,18]},calls=[];
 const network={generation:1,closed:false,directories:new Map([['one',entry]]),enqueue:async work=>work(),request:async(target,operation)=>{calls.push([target,operation]);throw Object.assign(Error('503'),{status:503,reason:'storage unavailable'});}};
 const reader=await createIPTrackReadSession({network,directory:{key:'one',capabilitiesVerified:true,publicKey:key,delivery,epoch},assertCurrent(){}});
 await assert.rejects(reader.request(2,[new Uint8Array(32),null]),{code:TRACK_STORAGE_UNAVAILABLE,status:503});
 assert.deepEqual(calls,[[entry,2]]);reader.close();
});

test('all eleven locales explain read retries and retained unconfirmed publication with the real resume label',()=>{
 for(const {id} of locales){const read=trackStorageErrorText(id,TRACK_STORAGE_UNAVAILABLE),resume=geocachingIpPublicationText(id).resume,publish=trackStorageErrorText(id,TRACK_STORAGE_UNAVAILABLE,{publication:true,resume});assert.ok(read&&publish&&read!==publish);assert.ok(publish.includes(resume));assert.ok(!publish.includes('{resume}'));assert.equal(trackStorageErrorText(id,'DIRECTORY_REJECTED'),null);}
 assert.throws(()=>trackStorageErrorText('unknown',TRACK_STORAGE_UNAVAILABLE));
 assert.throws(()=>trackStorageErrorText('en',TRACK_STORAGE_UNAVAILABLE,{publication:true}));
});
