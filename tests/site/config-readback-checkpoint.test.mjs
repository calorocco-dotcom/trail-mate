import test from 'node:test';import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {CONFIG_READBACK_STORAGE_KEY,saveConfigurationReadbackCheckpoint,loadConfigurationReadbackCheckpoint,confirmConfigurationReadbackCheckpoint} from '../../site/device/src/config-readback-checkpoint.js';
import {createConfigurationReadback,checkConfigurationReadback} from '../../site/device/src/config-readback.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
const document=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmt.mqtt.password=str:never-persist-this-secret\nEND\n'));
const author={verified:true,fingerprint:'a'.repeat(64),generation:7};
const storage=()=>{const data=new Map();return {data,getItem:key=>data.get(key)??null,setItem:(key,value)=>data.set(key,value),removeItem:key=>data.delete(key)};};

test('confirmed f32 readback updates only the matching transfer digest and remains secret-free',async()=>{
 const make=value=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmc.frequency_mhz=f32:'+value+'\nmt.mqtt.password=str:never-persist-this-secret\nEND\n'));
 const expected=make(495.2),actual=make('495.200012'),check=createConfigurationReadback(expected,author);check.transferId=new Uint8Array(16).fill(7);const store=storage(),hash=async doc=>new Uint8Array(await webcrypto.subtle.digest('SHA-256',doc.serialize())),originalHash=await hash(expected),actualHash=await hash(actual);assert.notDeepEqual(originalHash,actualHash);
 saveConfigurationReadbackCheckpoint(store,check,originalHash,check.transferId);const result=checkConfigurationReadback(check,actual,author);assert.equal(result.status,'matched');assert.equal(confirmConfigurationReadbackCheckpoint(store,check,result,actualHash),true);assert.equal(checkConfigurationReadback(loadConfigurationReadbackCheckpoint(store),actual,author,actualHash).status,'matched');assert.ok(!store.getItem(CONFIG_READBACK_STORAGE_KEY).includes('never-persist'));
 for(const status of ['mismatch','wrongDevice','unverified']){const before=store.getItem(CONFIG_READBACK_STORAGE_KEY);assert.equal(confirmConfigurationReadbackCheckpoint(store,check,{status},originalHash),false);assert.equal(store.getItem(CONFIG_READBACK_STORAGE_KEY),before);}
 saveConfigurationReadbackCheckpoint(store,check,actualHash,new Uint8Array(16).fill(8));const newer=store.getItem(CONFIG_READBACK_STORAGE_KEY);assert.equal(confirmConfigurationReadbackCheckpoint(store,check,result,originalHash),false);assert.equal(store.getItem(CONFIG_READBACK_STORAGE_KEY),newer);
});
test('refresh checkpoint contains only whole-document digest, public author fingerprint and transfer ID',async()=>{
 const doc=document(),hash=new Uint8Array(await webcrypto.subtle.digest('SHA-256',doc.serialize())),store=storage();
 saveConfigurationReadbackCheckpoint(store,createConfigurationReadback(doc,author),hash,new Uint8Array(16).fill(1));
 const raw=store.getItem(CONFIG_READBACK_STORAGE_KEY);
 assert.ok(!raw.includes('never-persist'));assert.ok(!raw.includes('mt.mqtt'));assert.ok(!raw.includes('TMSET7'));
 const restored=loadConfigurationReadbackCheckpoint(store);
 assert.deepEqual(Object.keys(restored).sort(),['fingerprint','sha256','transferId','version']);
 assert.equal(checkConfigurationReadback(restored,doc,{...author,generation:8},hash).status,'matched');
 const different=hash.slice();different[0]^=1;
 assert.equal(checkConfigurationReadback(restored,doc,author,different).status,'mismatchSummary');
 assert.equal(checkConfigurationReadback(restored,doc,{...author,fingerprint:'b'.repeat(64)},hash).status,'wrongDevice');
 assert.equal(checkConfigurationReadback(restored,doc,undefined,hash).status,'unverified');
});
test('corrupt, oversized, future or expanded checkpoint is removed without trusting its state',()=>{
 const valid={version:1,fingerprint:author.fingerprint,sha256:'b'.repeat(64),transferId:'c'.repeat(32)};
 for(const value of ['{','x'.repeat(513),JSON.stringify({...valid,version:2}),JSON.stringify({...valid,expected:'secret'}),JSON.stringify({...valid,sha256:'bad'})]){
  const store=storage();store.setItem(CONFIG_READBACK_STORAGE_KEY,value);store.setItem('unrelated','keep');
  assert.equal(loadConfigurationReadbackCheckpoint(store),undefined);assert.equal(store.getItem(CONFIG_READBACK_STORAGE_KEY),null);assert.equal(store.getItem('unrelated'),'keep');
 }
 assert.equal(loadConfigurationReadbackCheckpoint({getItem(){throw Error('Denied');},removeItem(){throw Error('Denied');}}),undefined);
});
test('failed checkpoint replacement removes an older intent when storage permits removal',()=>{
 const store=storage();store.setItem(CONFIG_READBACK_STORAGE_KEY,'older intent');
 store.setItem=()=>{throw Error('Quota exceeded');};
 assert.throws(()=>saveConfigurationReadbackCheckpoint(store,createConfigurationReadback(document(),author),new Uint8Array(32),new Uint8Array(16)),/Quota exceeded/);
 assert.equal(store.getItem(CONFIG_READBACK_STORAGE_KEY),null);
});
