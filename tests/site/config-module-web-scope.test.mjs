import test from 'node:test';import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {isWebModuleField,preserveExcludedModuleConfiguration} from '../../site/device/src/config-module-web-scope.js';
import {readMeshtasticModule,replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
import {readStructuredModule,updateStructuredModuleField,setStructuredModulePresence} from '../../site/device/src/meshtastic-module-model.js';
import {readProtobufMessage,protobufBytesField,protobufVarintField,patchSingularProtobufField} from '../../site/device/src/module-protobuf-wire.js';
import {MODULE_SCHEMA} from '../../site/device/src/meshtastic-module-schema.js';
import {configurationModuleSearchRows} from '../../site/device/src/config-module-search.js';
import {configurationModuleReview} from '../../site/device/src/config-module-review.js';
import {configModuleText,configModuleFieldLabel} from '../../site/i18n/config-module-labels.js';import {locales} from '../../site/i18n/registry.js';
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Old\nmt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:QAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\nEND\n'));
const value=(document,path)=>path.reduce((model,key)=>model.fields.find(field=>field.name===key).value,readStructuredModule(readMeshtasticModule(document)));
const rootField=MODULE_SCHEMA.messages[MODULE_SCHEMA.root].fields.find(field=>field.name==='paxcounter'),leaf=MODULE_SCHEMA.messages[rootField.message].fields.find(field=>field.name==='ble_threshold');
test('module scope excludes BLE paths in every language from metadata search and change preview',()=>{
 const current=updateStructuredModuleField(fixture(),['paxcounter','ble_threshold'],-80),changed=updateStructuredModuleField(current,['paxcounter','ble_threshold'],-50);
 assert.equal(isWebModuleField(['paxcounter','ble_threshold']),false);assert.equal(isWebModuleField(['paxcounter','wifi_threshold']),true);
 for(const {id} of locales){const rows=configurationModuleSearchRows(changed,current,{label:(path,kind)=>configModuleFieldLabel(path,id,kind),text:configModuleText(id),group:'radio',groupLabel:'Radio'});assert.ok(!rows.some(row=>row.key==='module:paxcounter.ble_threshold'));assert.ok(rows.some(row=>row.key==='module:paxcounter.wifi_threshold'));assert.deepEqual(configurationModuleReview(changed,current,id),[]);}
 assert.equal(value(changed,['paxcounter','ble_threshold']),-50,'presentation does not destroy source data');
});
test('known backup import restores visible edits while retaining hidden signed values and unknown bytes',()=>{
 let current=updateStructuredModuleField(fixture(),['paxcounter','ble_threshold'],-80);const original=readMeshtasticModule(current),unknown=protobufVarintField(199,91);current=replaceMeshtasticModule(current,Uint8Array.from([...original,...unknown]));
 const backup=new TmsDocument(updateStructuredModuleField(updateStructuredModuleField(current,['paxcounter','ble_threshold'],-50),['paxcounter','wifi_threshold'],-60).serialize({kind:'backup'}));backup.set('device.node_name','Imported');
 const result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);assert.equal(value(result.document,['paxcounter','ble_threshold']),-80);assert.equal(value(result.document,['paxcounter','wifi_threshold']),-60);assert.equal(result.document.get('device.node_name').value,'Imported');assert.ok(result.skipped.some(row=>row.key==='mt_module'));assert.ok(readProtobufMessage(readMeshtasticModule(result.document)).some(frame=>frame.number===199&&frame.value===91n));assert.equal(value(current,['paxcounter','wifi_threshold']),0);
});
test('absent hidden fields cannot be injected; existing hidden fields survive an absent backup parent without enabling it',()=>{
 const current=fixture(),added=updateStructuredModuleField(current,['paxcounter','ble_threshold'],-50),restored=preserveExcludedModuleConfiguration(current,added);assert.equal(value(restored.document,['paxcounter','ble_threshold']),0);assert.equal(readStructuredModule(readMeshtasticModule(restored.document)).fields.find(field=>field.name==='paxcounter').value.fields.find(field=>field.name==='ble_threshold').present,false);
 const existing=updateStructuredModuleField(current,['paxcounter','ble_threshold'],-80),removed=setStructuredModulePresence(existing,['paxcounter'],false),retained=preserveExcludedModuleConfiguration(existing,removed);assert.equal(value(retained.document,['paxcounter','ble_threshold']),-80);assert.equal(value(retained.document,['paxcounter','enabled']),false);
 assert.equal(preserveExcludedModuleConfiguration(existing,existing).changed,false);
});
test('excluded leaf wrong-wire data is ignored while nonexcluded corruption rolls the whole import back',()=>{
 const current=updateStructuredModuleField(fixture(),['paxcounter','ble_threshold'],-80),bytes=readMeshtasticModule(current),wrongLeaf=patchSingularProtobufField(bytes,[rootField.number,leaf.number],protobufBytesField(leaf.number,new Uint8Array([1,2]))),backup=new TmsDocument(replaceMeshtasticModule(current,wrongLeaf).serialize({kind:'backup'}));backup.set('device.node_name','Permitted');const restored=prepareConfigImport(current,backup);assert.deepEqual(restored.errors,[]);assert.equal(value(restored.document,['paxcounter','ble_threshold']),-80);assert.equal(restored.document.get('device.node_name').value,'Permitted');
 const wifi=MODULE_SCHEMA.messages[rootField.message].fields.find(field=>field.name==='wifi_threshold'),wrongOther=patchSingularProtobufField(bytes,[rootField.number,wifi.number],protobufBytesField(wifi.number,new Uint8Array([1]))),corrupt=new TmsDocument(replaceMeshtasticModule(current,wrongOther).serialize({kind:'backup'}));corrupt.set('device.node_name','Rejected');const rejected=prepareConfigImport(current,corrupt);assert.equal(rejected.errors.length,1);assert.deepEqual(rejected.changed,[]);assert.equal(rejected.document.get('device.node_name').value,'Old');
});
