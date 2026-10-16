import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {readTrackVersionHistory} from '../../site/tracks/src/track-version-history.js';
import {trackVersionHistoryText} from '../../site/i18n/track-version-history-labels.js';
import {locales} from '../../site/i18n/registry.js';
const hex=value=>Buffer.from(value).toString('hex');
async function fixture(count,{upgrade=false,archive=false}={}){
 const author=await Identity.generate(),pub=await author.getPublicKey(),versions=[];
 for(let revision=1;revision<=count;revision++){
  const version=upgrade&&revision>1?2:1,record=[version,pub,new Uint8Array(16).fill(7),0,revision,versions.at(-1)?.revisionHash??null,archive&&revision===count?1:0,100,100+revision,'Version '+revision,'',[0,0,0,0],[0,null,null,1],[new Uint8Array(32).fill(1),123,'application/vnd.google-earth.kml+xml',1]];
  if(version===2)record.push([0,[],0,0,0,0,0,null],null);
  const raw=encodeTrackManifest(record),signed=[raw,await author.sign(trackManifestSigningBytes(raw))];versions.push(await verifyTrackManifest(signed));
 }
 const byHash=new Map(versions.map(value=>[hex(value.revisionHash),value])),calls=[];
 const request=async(operation,body)=>{calls.push([operation,body]);assert.equal(operation,2);const result=byHash.get(hex(body[1]));return[[result.manifest,result.signature],1];};
 return{author,versions,byHash,calls,request,selected:versions.at(-1)};
}

test('the exact v1/v2/archive chain reaches the signed root using metadata only',async()=>{
 const f=await fixture(3,{upgrade:true,archive:true}),progress=[],result=await readTrackVersionHistory(f.request,f.selected,{onProgress:value=>progress.push(value)});
 assert.equal(result.complete,true);assert.equal(result.status,'complete');assert.deepEqual(result.versions.map(value=>value.record[4]),[3,2,1]);assert.deepEqual(result.versions.map(value=>value.record[0]),[2,2,1]);assert.equal(result.filesVerified,false);assert.equal(result.otherBranchesVerified,false);assert.equal(f.calls.length,2);assert.deepEqual(progress.map(value=>value.versions.length),[1,2,3]);
 for(const [,body] of f.calls)assert.deepEqual(body[0],f.selected.objectId);
});

test('the first version is reverified locally without asking for files or parents',async()=>{
 const f=await fixture(1),result=await readTrackVersionHistory(()=>assert.fail('Unexpected request'),f.selected);assert.equal(result.complete,true);assert.equal(result.versions.length,1);
 const bad={...f.selected,signature:new Uint8Array(64)};await assert.rejects(readTrackVersionHistory(()=>assert.fail('Unexpected request'),bad),{code:'TRACK_HISTORY_PROOF_INVALID'});
});

test('wrong signed parent, broken signature and malformed response never enter the verified prefix',async()=>{
 const f=await fixture(3),foreign=await fixture(1);
 for(const response of [[[foreign.selected.manifest,foreign.selected.signature],0],[[f.versions[1].manifest,new Uint8Array(64)],0],[[f.versions[0].manifest,f.versions[0].signature],0],[null,1],[[f.versions[1].manifest,f.versions[1].signature],2]]){
  const progress=[];await assert.rejects(readTrackVersionHistory(async()=>response,f.selected,{onProgress:value=>progress.push(value)}),{code:'TRACK_HISTORY_PROOF_INVALID'});assert.deepEqual(progress.map(value=>value.versions.length),[1]);
 }
});

test('cryptographically signed but invalid successor rules are rejected',async()=>{
 const f=await fixture(3),record=f.selected.record;
 for(const change of [value=>value[4]++,value=>value[7]++,value=>value[8]=100,value=>{value[6]=1;value[13][0]=new Uint8Array(32).fill(9);}]){
  const changed=structuredClone(record);change(changed);const raw=encodeTrackManifest(changed),head=await verifyTrackManifest([raw,await f.author.sign(trackManifestSigningBytes(raw))]);
  await assert.rejects(readTrackVersionHistory(f.request,head),{code:'TRACK_HISTORY_PROOF_INVALID'});
 }
});

test('a missing parent preserves only the verified portion; unrelated service errors remain distinct',async()=>{
 const f=await fixture(3),result=await readTrackVersionHistory(async(op,body)=>{if(hex(body[1])===hex(f.versions[0].revisionHash))throw Object.assign(Error('missing'),{status:422,reason:'version missing'});return f.request(op,body);},f.selected);
 assert.equal(result.complete,false);assert.equal(result.status,'missing');assert.deepEqual(result.versions.map(value=>value.record[4]),[3,2]);
 const storage=Object.assign(Error('storage'),{code:'TRACK_DIRECTORY_STORAGE_UNAVAILABLE',status:503,reason:'storage unavailable'});await assert.rejects(readTrackVersionHistory(async()=>{throw storage},f.selected),error=>error===storage);
});

test('the 64-version budget never claims a root or requests an unbounded history',async()=>{
 const f=await fixture(65),result=await readTrackVersionHistory(f.request,f.selected);assert.equal(result.status,'limited');assert.equal(result.complete,false);assert.equal(result.versions.length,64);assert.equal(f.calls.length,63);assert.equal(result.versions.at(-1).record[4],2);
 await assert.rejects(readTrackVersionHistory(f.request,f.selected,{maxVersions:65}),{code:'TRACK_HISTORY_OPTIONS_INVALID'});
});

test('cancelled responses cannot extend the verified prefix; progress mutations cannot rewrite verification',async()=>{
 const f=await fixture(3),controller=new AbortController(),progress=[];
 await assert.rejects(readTrackVersionHistory(async(op,body)=>{const result=await f.request(op,body);controller.abort();return result;},f.selected,{signal:controller.signal,onProgress:value=>progress.push(value)}),{name:'AbortError'});assert.equal(progress.length,1);
 const safe=await readTrackVersionHistory(f.request,f.selected,{onProgress:value=>{value.versions[0].record[4]=1;value.versions[0].manifest.fill(0);value.versions[0].revisionHash.fill(0);value.objectId.fill(0);}});assert.equal(safe.complete,true);assert.deepEqual(safe.versions.map(value=>value.record[4]),[3,2,1]);
});

test('history and branch status have complete distinct text for all supported languages',()=>{
 for(const {id} of locales){const text=trackVersionHistoryText(id);assert.equal(Object.keys(text).length,15);for(const value of Object.values(text))assert.ok(typeof value==='string'&&value.length);assert.ok(text.reading.includes('{count}'));assert.ok(text.limited.includes('64'));assert.notEqual(text.complete,text.branch);}
});
