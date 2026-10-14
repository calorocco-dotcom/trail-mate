import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationModuleSearchRows} from '../../site/device/src/config-module-search.js';
import {findConfigurationFields} from '../../site/device/src/config-finder.js';
import {updateStructuredModuleField,editStructuredModuleRow,setStructuredModulePresence} from '../../site/device/src/meshtastic-module-model.js';
import {configModuleText,configModuleFieldLabel} from '../../site/i18n/config-module-labels.js';
import {locales} from '../../site/i18n/registry.js';
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:QAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\nEND\n'));
const rows=(document,baseline,locale='zh-Hans')=>configurationModuleSearchRows(document,baseline,{label:(path,kind)=>configModuleFieldLabel(path,locale,kind),text:configModuleText(locale),group:'radio',groupLabel:'Radio'});
test('absent modules are searchable without indexing values or adding settings',()=>{
 const source=fixture(),before=source.serialize(),result=rows(source,source);
 const address=result.find(row=>row.key==='module:mqtt.address');assert.ok(address);assert.equal(address.targetId,'module-mqtt');assert.equal(address.changed,false);
 assert.deepEqual(findConfigurationFields(result,'MQTT 地址'),[address]);assert.deepEqual(source.serialize(),before);
 assert.ok(result.every(row=>!('value' in row)&&!('raw' in row)));
});
test('present and nested fields point at actual controls; changed-only is per field and excludes secrets',()=>{
 const baseline=fixture(),document=updateStructuredModuleField(updateStructuredModuleField(baseline,['mqtt','password'],'synthetic-hidden-value'),['mqtt','address'],'private-broker-value');
 const result=rows(document,baseline),changed=new Set(result.filter(row=>row.changed).map(row=>row.key));
 assert.equal(result.find(row=>row.key==='module:mqtt.address').targetId,'module-mqtt-address');
 assert.ok(findConfigurationFields(result,'MQTT 地址',{changedOnly:true,changes:changed}).length===1);
 assert.equal(findConfigurationFields(result,'synthetic-hidden-value').length,0);assert.equal(findConfigurationFields(result,'private-broker-value').length,0);
 assert.ok(!JSON.stringify(result).includes('synthetic-hidden-value'));
 const nested=updateStructuredModuleField(document,['mqtt','map_report_settings','position_precision'],16),nestedRows=rows(nested,baseline);
 assert.equal(nestedRows.find(row=>row.key==='module:mqtt.map_report_settings.position_precision').targetId,'module-mqtt-map_report_settings-position_precision');
 for(const locale of locales){const translated=rows(document,baseline,locale.id);assert.equal(translated.length,result.length);assert.ok(translated.every(row=>row.label&&row.help));}
});
test('GPIO entries keep indexed keys and targets; removals remain discoverable in changed-only search',()=>{
 let baseline=editStructuredModuleRow(fixture(),['remote_hardware','available_pins'],0,{action:'insert',values:{gpio_pin:13,name:'Lamp'}});
 baseline=editStructuredModuleRow(baseline,['remote_hardware','available_pins'],1,{action:'insert',values:{gpio_pin:14}});
 const document=editStructuredModuleRow(baseline,['remote_hardware','available_pins'],1,{action:'remove'}),result=rows(document,baseline);
 assert.equal(result.find(row=>row.key==='module:remote_hardware.available_pins:0:gpio_pin').targetId,'module-remote_hardware-available_pins-0-gpio_pin');
 assert.equal(result.find(row=>row.key==='module:remote_hardware.available_pins').changed,true);
 const removed=setStructuredModulePresence(document,['remote_hardware'],false),absent=rows(removed,baseline);
 assert.equal(absent.find(row=>row.key==='module:remote_hardware.available_pins:0:gpio_pin').targetId,'module-remote_hardware');
});
test('unsupported and corrupt module storage retains raw data without attempting structured navigation',()=>{
 const source=fixture();source.set('mt_module.format','future-format');const before=source.serialize();
 assert.deepEqual(rows(source,fixture()),[]);assert.deepEqual(source.serialize(),before);
 const corrupt=fixture();corrupt.set('mt_module.chunk.0',new Uint8Array([255]));const retained=corrupt.serialize();
 assert.deepEqual(rows(corrupt,fixture()),[]);assert.deepEqual(corrupt.serialize(),retained);
});
