import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {setConfigurationValue,synchronizeMeshCoreChannelMirror} from '../../site/device/src/config-editor-values.js';
import {configurationChanges} from '../../site/device/src/config-collections.js';

const executable=path.resolve('.codex-build/hostlink-codec/native/tms_config_interop_cli'+(process.platform==='win32'?'.exe':''));
function run(args,input){const result=spawnSync(executable,args,{input,maxBuffer:16*1024*1024,windowsHide:true});if(result.error)throw result.error;assert.equal(result.status,0,String(result.stderr));return result.stdout;}
const baseline=new Uint8Array(run(['--emit']));
function batch(inputs){
 const frames=inputs.map(bytes=>{const header=Buffer.alloc(4);header.writeUInt32LE(bytes.length);return Buffer.concat([header,bytes]);}),output=run(['--batch'],Buffer.concat(frames)),results=[];
 let offset=0;while(offset<output.length){assert.ok(offset+5<=output.length);const status=output[offset],size=output.readUInt32LE(offset+1);offset+=5;assert.ok(offset+size<=output.length);results.push({status,bytes:new Uint8Array(output.subarray(offset,offset+size))});offset+=size;}
 assert.equal(results.length,inputs.length);return results;
}
function alternate(record){
 const spec=configurationField(record),value=record.value;
 if(spec.readOnly)return null;
 if(spec.options)return spec.options.find(option=>option.value!==value)?.value??null;
 if(record.type==='bool')return !value;
 if(record.type==='str')return value==='测试 %&/?'?'另一个名称':'测试 %&/?';
 if(record.type==='b64')return Uint8Array.from({length:value.length||16},(_,index)=>(index*13+71)%256);
 if(record.type==='f32')return Math.fround(value===1.25?2.5:1.25);
 if(spec.range)return value===1?2:1;
 throw Error('Missing alternate for '+record.key);
}

test('each writable core TMS field edited by the browser survives actual firmware decoding and model re-emission',()=>{
 const entries=new TmsDocument(baseline).entries(),cases=[];
 for(const record of entries){const value=alternate(record);if(value===null)continue;validateConfigurationField(record,value);const document=new TmsDocument(baseline);setConfigurationValue(document,record.key,value);cases.push({key:record.key,value,bytes:document.serialize()});}
 assert.ok(cases.length>200,'Core writer unexpectedly lost fields');
 const replies=batch(cases.map(item=>item.bytes)),failures=[];
 for(let index=0;index<cases.length;index++){
  const item=cases[index],reply=replies[index];if(reply.status!==0){failures.push(item.key+': native rejection '+reply.status);continue;}
  const restored=new TmsDocument(reply.bytes),actual=restored.get(item.key).value;try{assert.deepEqual(actual,item.value);assert.deepEqual([...configurationChanges(restored,new TmsDocument(item.bytes))],[]);}catch{failures.push(item.key+': changed after model decode/re-emission');}
 }
 assert.deepEqual(failures,[]);
 console.log('Core field roundtrip coverage: '+cases.length+' writable fields; synthetic values only, actual C++ codec/model.');
});

test('web constraints agree with real core capacities and MeshCore slot admission',()=>{
 const fixtures=[];
 const add=(key,value,accepted)=>{const document=new TmsDocument(baseline),record=document.get(key);if(accepted)validateConfigurationField(record,value);else assert.throws(()=>validateConfigurationField(record,value));document.set(key,value);fixtures.push({key,value,accepted,bytes:document.serialize()});};
 for(const slot of [0,7])add('mc.active_slot',slot,true);
 for(const slot of [8,255])add('mc.active_slot',slot,false);
 for(const length of [0,1,31,33])add('legacy.secondary_psk',new Uint8Array(length),false);
 add('legacy.secondary_psk',new Uint8Array(32).fill(7),true);
 for(const length of [0,255])add('aprs.node_map',new Uint8Array(length).fill(19),true);
 add('aprs.node_map',new Uint8Array(256),false);
 const replies=batch(fixtures.map(item=>item.bytes));
 for(let index=0;index<fixtures.length;index++){const fixture=fixtures[index],reply=replies[index];
  if(fixture.accepted){assert.equal(reply.status,0);assert.deepEqual(new TmsDocument(reply.bytes).get(fixture.key).value,fixture.value);}
  else assert.equal(reply.status,1);
 }
});

test('MeshCore compatibility lengths and padded keys match actual firmware model normalization',()=>{
 const documents=[0,16,32].map(length=>{const document=new TmsDocument(baseline);document.set('mc.base.secondary_psk',new Uint8Array(length));setConfigurationValue(document,'mc.active_slot',0);setConfigurationValue(document,'mc.channel.0.key',new Uint8Array(16).fill(91));return document;});
 const replies=batch(documents.map(document=>document.serialize()));
 replies.forEach((reply,index)=>{assert.equal(reply.status,0);assert.deepEqual([...configurationChanges(new TmsDocument(reply.bytes),documents[index])],[]);});
 const stale=new TmsDocument(baseline);stale.set('mc.base.secondary_psk',new Uint8Array(16));stale.set(`mc.channel.${stale.get('mc.active_slot').value}.key`,new Uint8Array(16).fill(91));const [changed]=batch([stale.serialize()]);assert.equal(changed.status,0);assert.deepEqual([...configurationChanges(new TmsDocument(changed.bytes),stale)],['mc.base.secondary_psk']);synchronizeMeshCoreChannelMirror(stale);assert.deepEqual([...configurationChanges(new TmsDocument(changed.bytes),stale)],[]);
});

test('native current schema rejects missing records, duplicate records and wrong types before applying',()=>{
 const text=new TextDecoder().decode(baseline),line='device.node_name=str:',begin=text.indexOf(line),end=text.indexOf('\n',begin)+1;
 assert.ok(begin>0);const encode=text=>new TextEncoder().encode(text);
 const invalid=[text.slice(0,begin)+text.slice(end),text.slice(0,end)+text.slice(begin,end)+text.slice(end),text.replace(line,'device.node_name=u8:'),text.replace('END\n','device.node_name=str:Extra\nEND\n')];
 for(const reply of batch(invalid.map(encode))){assert.equal(reply.status,1);assert.equal(reply.bytes.length,0);}
});
