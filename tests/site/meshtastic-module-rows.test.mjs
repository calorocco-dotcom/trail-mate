import test from 'node:test';import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';import {readMeshtasticModule,replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
import {readStructuredModule,editStructuredModuleRow} from '../../site/device/src/meshtastic-module-model.js';import {protobufVarintField,protobufBytesField,readProtobufMessage} from '../../site/device/src/module-protobuf-wire.js';
const field=(message,name)=>message.fields.find(item=>item.name===name);
const rows=document=>field(field(readStructuredModule(readMeshtasticModule(document)),'remote_hardware').value,'available_pins').value;
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nfuture.module=opaque:preserved\nmt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:QAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\nEND\n'));
const path=['remote_hardware','available_pins'];
test('repeated rows insert, update and remove without changing other configuration',()=>{
 const original=fixture(),before=original.serialize();let document=editStructuredModuleRow(original,path,0,{action:'insert',values:{gpio_pin:12,name:'Sensor',type:1}});
 document=editStructuredModuleRow(document,path,1,{action:'insert',values:{gpio_pin:13,name:'Relay',type:2}});
 document=editStructuredModuleRow(document,path,0,{values:{name:'温度'}});assert.equal(field(rows(document)[0],'name').value,'温度');assert.equal(field(rows(document)[1],'gpio_pin').value,13);
 document=editStructuredModuleRow(document,path,0,{action:'remove'});assert.equal(rows(document).length,1);assert.equal(field(rows(document)[0],'name').value,'Relay');assert.equal(document.get('future.module').raw,'preserved');assert.deepEqual(original.serialize(),before);
});
test('unknown row bytes and other rows survive targeted modification',()=>{
 const unknown=protobufVarintField(99,77),first=Uint8Array.from([...protobufVarintField(1,12),...unknown]),second=protobufVarintField(1,13);
 const remote=Uint8Array.from([...protobufBytesField(3,first),...protobufVarintField(2,1),...protobufBytesField(3,second)]);
 const document=replaceMeshtasticModule(fixture(),protobufBytesField(10,remote)),updated=editStructuredModuleRow(document,path,0,{values:{gpio_pin:14}});
 assert.deepEqual(rows(updated)[0].unknown,[unknown]);
 const frames=readProtobufMessage(readProtobufMessage(readMeshtasticModule(updated))[0].value);assert.deepEqual(frames.map(item=>item.number),[3,2,3]);assert.deepEqual(frames[2].value,second);
});
test('repeated capacity, row index, string storage and enum bounds reject atomically',()=>{
 let document=fixture();for(let index=0;index<4;index++)document=editStructuredModuleRow(document,path,index,{action:'insert',values:{gpio_pin:index,type:1}});
 const before=document.serialize();for(const [index,options] of [[4,{action:'insert'}],[-1,{action:'remove'}],[4,{action:'remove'}],[0,{values:{gpio_pin:256}}],[0,{values:{name:'界'.repeat(5)}}],[0,{values:{type:99}}]])assert.throws(()=>editStructuredModuleRow(document,path,index,options));assert.deepEqual(document.serialize(),before);
});
