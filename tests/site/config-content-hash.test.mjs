import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationContentHash} from '../../site/device/src/config-content-hash.js';
import {configurationChanges} from '../../site/device/src/config-collections.js';
import {createConfigurationReadback,checkConfigurationReadback} from '../../site/device/src/config-readback.js';
import {saveConfigurationReadbackCheckpoint,loadConfigurationReadbackCheckpoint,CONFIG_READBACK_STORAGE_KEY} from '../../site/device/src/config-readback-checkpoint.js';
const author={verified:true,fingerprint:'ab'.repeat(32)},encoder=new TextEncoder();
const fields=['schema.version=u16:7','document.kind=enum:working','device.node_name=str:Camp','mc.frequency_mhz=f32:495.2','mt.mqtt.password=str:secret%20value','gps.enabled=bool:0','value.bytes=b64:AAEC','vendor.future=opaque:keep'];
const document=(lines=fields)=>new TmsDocument(encoder.encode('TMSET7\n'+lines.join('\n')+'\nEND\n'));
const store=()=>{const data=new Map();return {getItem:key=>data.get(key)??null,setItem:(key,value)=>data.set(key,value),removeItem:key=>data.delete(key)};};
test('typed digest matches exact config comparisons across order, float32 text and UTF-8 escape differences',async()=>{
 const source=document(),hash=await configurationContentHash(source),equivalent=document([...fields].reverse().map(line=>line.replace('f32:495.2','f32:495.200012').replace('str:Camp','str:%43amp').replace('secret%20value','secret%20%76alue')));
 assert.deepEqual([...configurationChanges(source,equivalent)],[]);assert.equal(await configurationContentHash(equivalent),hash);
 const zero=document(fields.map(line=>line.replace('f32:495.2','f32:0'))),negativeZero=document(fields.map(line=>line.replace('f32:495.2','f32:-0')));assert.equal(await configurationContentHash(zero),await configurationContentHash(negativeZero));
 assert.deepEqual(source.entries().map(record=>record.raw),document().entries().map(record=>record.raw));
});
test('digest catches every typed change including one float32 step, missing fields and unknown raw values',async()=>{
 const source=document(),hash=await configurationContentHash(source);
 for(const [from,to] of [['str:Camp','str:Else'],['f32:495.2','f32:495.200043'],['str:secret%20value','str:changed'],['bool:0','bool:1'],['b64:AAEC','b64:AAED'],['opaque:keep','opaque:changed'],['device.node_name=str:Camp','device.node_name=opaque:Camp']]){const changed=document(fields.map(line=>line.replace(from,to)));assert.notEqual(await configurationContentHash(changed),hash);assert.ok(configurationChanges(source,changed).size>0);}
 assert.notEqual(await configurationContentHash(document(fields.slice(0,-1))),hash);
});
test('version 2 refresh checkpoint verifies reordered native content without persisting secrets',async()=>{
 const source=document(),check=createConfigurationReadback(source,author);check.contentSha256=await configurationContentHash(source);const storage=store();saveConfigurationReadbackCheckpoint(storage,check,new Uint8Array(32).fill(1),new Uint8Array(16).fill(2));
 const saved=loadConfigurationReadbackCheckpoint(storage),actual=document([...fields].reverse().map(line=>line.replace('f32:495.2','f32:495.200012'))),actualContent=await configurationContentHash(actual);
 assert.equal(saved.version,2);assert.deepEqual(Object.keys(saved).sort(),['contentSha256','fingerprint','sha256','transferId','version']);
 const raw=storage.getItem(CONFIG_READBACK_STORAGE_KEY);assert.ok(!raw.includes('secret'));assert.ok(!raw.includes('mt.mqtt'));assert.ok(!raw.includes('TMSET7'));
 assert.equal(checkConfigurationReadback(saved,actual,{...author,generation:9},new Uint8Array(32).fill(9),actualContent).status,'matched');
 assert.throws(()=>checkConfigurationReadback(saved,actual,author,new Uint8Array(32)),/content hash/);
 assert.equal(checkConfigurationReadback(saved,actual,{...author,fingerprint:'cd'.repeat(32)},undefined,actualContent).status,'wrongDevice');assert.equal(checkConfigurationReadback(saved,actual,undefined,undefined,actualContent).status,'unverified');
 const changed=document(fields.map(line=>line.replace('str:Camp','str:Else')));assert.equal(checkConfigurationReadback(saved,changed,author,undefined,await configurationContentHash(changed)).status,'mismatchSummary');
});
test('version 2 malformed content digest or extra fields never load',()=>{
 const valid={version:2,fingerprint:author.fingerprint,sha256:'a'.repeat(64),transferId:'b'.repeat(32),contentSha256:'c'.repeat(64)};
 for(const value of [{...valid,contentSha256:undefined},{...valid,contentSha256:'bad'},{...valid,expected:'secret'},{...valid,version:3}]){const storage=store();storage.setItem(CONFIG_READBACK_STORAGE_KEY,JSON.stringify(value));assert.equal(loadConfigurationReadbackCheckpoint(storage),undefined);assert.equal(storage.getItem(CONFIG_READBACK_STORAGE_KEY),null);}
});
