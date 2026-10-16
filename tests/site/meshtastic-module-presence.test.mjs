import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {readMeshtasticModule,replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
import {readStructuredModule,setStructuredModulePresence,updateStructuredModuleField} from '../../site/device/src/meshtastic-module-model.js';
import {protobufVarintField} from '../../site/device/src/module-protobuf-wire.js';
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:QAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\nEND\n'));
const field=(document,name)=>readStructuredModule(readMeshtasticModule(document)).fields.find(item=>item.name===name);
test('optional module presence differs from disabled and preserves populated modules',()=>{
 const source=fixture(),added=setStructuredModulePresence(source,['mqtt'],true);
 assert.equal(field(source,'mqtt').present,false);assert.equal(field(added,'mqtt').present,true);
 assert.equal(field(added,'mqtt').value.fields.find(item=>item.name==='enabled').value,false);
 const populated=updateStructuredModuleField(added,['mqtt','address'],'broker.example');
 assert.deepEqual(setStructuredModulePresence(populated,['mqtt'],true).serialize(),populated.serialize());
 assert.equal(field(setStructuredModulePresence(populated,['mqtt'],false),'mqtt').present,false);
 assert.equal(field(populated,'mqtt').present,true);
});
test('nested removal preserves other modules, version and opaque bytes without creating ancestors',()=>{
 const source=fixture();assert.deepEqual(setStructuredModulePresence(source,['mqtt','map_report_settings'],false).serialize(),source.serialize());
 let document=updateStructuredModuleField(source,['mqtt','map_report_settings','position_precision'],16);
 document=updateStructuredModuleField(document,['serial','enabled'],true);
 const opaque=protobufVarintField(100,23);document=replaceMeshtasticModule(document,Uint8Array.from([...readMeshtasticModule(document),...opaque]));
 const removed=setStructuredModulePresence(document,['mqtt','map_report_settings'],false),model=readStructuredModule(readMeshtasticModule(removed));
 assert.equal(field(removed,'mqtt').present,true);assert.equal(field(removed,'mqtt').value.fields.find(item=>item.name==='map_report_settings').present,false);
 assert.equal(field(removed,'serial').value.fields.find(item=>item.name==='enabled').value,true);
 assert.equal(field(removed,'version').value,1);assert.deepEqual(model.unknown,[opaque]);
});
test('presence cannot target primitive, internal or repeated fields',()=>{
 for(const path of [['version'],['mqtt','enabled'],['remote_hardware','available_pins'],['missing']])assert.throws(()=>setStructuredModulePresence(fixture(),path,true),error=>error.code==='MODULE_FIELD_PATH');
 assert.throws(()=>setStructuredModulePresence(fixture(),['mqtt'],1),error=>error.code==='MODULE_FIELD_PATH');
});
