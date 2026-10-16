import test from 'node:test';import assert from 'node:assert/strict';import {execFile} from 'node:child_process';import {promisify} from 'node:util';
import {fileURLToPath} from 'node:url';
import {MODULE_SCHEMA} from '../../site/device/src/meshtastic-module-schema.js';import {readStructuredModule,updateStructuredModuleField} from '../../site/device/src/meshtastic-module-model.js';
import {protobufVarintField,protobufBytesField} from '../../site/device/src/module-protobuf-wire.js';import {TmsDocument} from '../../site/device/src/tms-document.js';import {readMeshtasticModule,replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
const field=(model,name)=>model.fields.find(item=>item.name===name);
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:QAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\nEND\n'));
test('generated schema matches current nanopb definitions and storage capacities',async()=>{
 await promisify(execFile)(process.execPath,[fileURLToPath(new URL('../../scripts/generate_meshtastic_module_schema.mjs',import.meta.url)),'--check']);
 assert.equal(Object.keys(MODULE_SCHEMA.messages).length,16);assert.equal(MODULE_SCHEMA.messages[MODULE_SCHEMA.root].maxBytes,673);
 const mqtt=MODULE_SCHEMA.messages.meshtastic_ModuleConfig_MQTTConfig;assert.equal(field(mqtt,'address').maxBytes,63);assert.equal(field(mqtt,'password').maxBytes,31);assert.equal(field(mqtt,'root').maxBytes,31);
 assert.equal(field(MODULE_SCHEMA.messages.meshtastic_ModuleConfig_AudioConfig,'i2s_sck').max,255);
});
test('typed MQTT edits preserve source, internal version and nested unknown bytes',()=>{
 const source=fixture(),before=source.serialize();let document=updateStructuredModuleField(source,['mqtt','enabled'],true);document=updateStructuredModuleField(document,['mqtt','address'],'broker.example');document=updateStructuredModuleField(document,['mqtt','map_report_settings','position_precision'],16);
 const model=readStructuredModule(readMeshtasticModule(document)),mqtt=field(model,'mqtt').value;
 assert.equal(field(model,'version').value,1);assert.equal(field(mqtt,'enabled').value,true);assert.equal(field(mqtt,'address').value,'broker.example');assert.equal(field(field(mqtt,'map_report_settings').value,'position_precision').value,16);assert.deepEqual(source.serialize(),before);
 const unknown=protobufVarintField(99,23),withUnknown=replaceMeshtasticModule(source,Uint8Array.from([...protobufBytesField(1,unknown),...protobufVarintField(8,1)]));
 const updated=updateStructuredModuleField(withUnknown,['mqtt','enabled'],true);assert.deepEqual(field(readStructuredModule(readMeshtasticModule(updated)),'mqtt').value.unknown,[unknown]);
});
test('typed constraints reject oversized Unicode, enum holes, GPIO overflow and internal edits',()=>{
 const source=fixture();updateStructuredModuleField(source,['mqtt','password'],'界'.repeat(10));
 assert.throws(()=>updateStructuredModuleField(source,['mqtt','password'],'界'.repeat(11)),error=>error.code==='MODULE_FIELD_STRING');
 assert.throws(()=>updateStructuredModuleField(source,['mqtt','password'],'\ud800'),error=>error.code==='MODULE_FIELD_STRING');
 assert.throws(()=>updateStructuredModuleField(source,['audio','i2s_sck'],256),error=>error.code==='MODULE_FIELD_RANGE');
 assert.throws(()=>updateStructuredModuleField(source,['serial','mode'],999),error=>error.code==='MODULE_FIELD_ENUM');
 assert.throws(()=>updateStructuredModuleField(source,['version'],2),error=>error.code==='MODULE_FIELD_READ_ONLY');
 assert.throws(()=>updateStructuredModuleField(source,['remote_hardware','available_pins'],[]),error=>error.code==='MODULE_FIELD_REPEATED');
});
test('signed INT32 thresholds retain negative values through protobuf/TMS round trip',()=>{
 const document=updateStructuredModuleField(fixture(),['paxcounter','wifi_threshold'],-80),model=readStructuredModule(readMeshtasticModule(new TmsDocument(document.serialize())));
 assert.equal(field(field(model,'paxcounter').value,'wifi_threshold').value,-80);
});
test('malformed earlier duplicate scalar occurrences are rejected even when the last value is valid',()=>{
 const mqtt=Uint8Array.from([...protobufBytesField(1,new Uint8Array([1])),...protobufVarintField(1,1)]);
 assert.throws(()=>readStructuredModule(protobufBytesField(1,mqtt)),error=>error.code==='MODULE_FIELD_WIRE');
});
