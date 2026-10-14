import test from 'node:test';import assert from 'node:assert/strict';
import {isWebConfigurationField} from '../../site/device/src/config-web-scope.js';import {prepareConfigImport} from '../../site/device/src/config-import.js';import {configurationReview} from '../../site/device/src/config-review.js';import {findConfigurationFields} from '../../site/device/src/config-finder.js';import {TmsDocument} from '../../site/device/src/tms-document.js';import {locales} from '../../site/i18n/registry.js';
test('web scope excludes companion-app BLE families and enable switches while preserving radio and USB controls',()=>{
 for(const key of ['device.ble_enabled','mt_ble.mode','mt_ble.fixed_pin','mc_ble.pin','mc_ble.telemetry_base','mc_ble.telemetry_location','mc_ble.telemetry_environment','mc_ble.manual_add_contacts','mc_ble.advert_location_policy','mc_ble.future','ble.enabled','bluetooth.profile','c6_companion.ble_enabled'])assert.equal(isWebConfigurationField(key),false,key);
 for(const key of ['device.node_name','device.short_name','mc.active_slot','mc.channel.0.key','mc.send_profile','mt.modem_preset','rt.interface_policy','wifi.enabled','c6_companion.wifi_enabled','gps.enabled'])assert.equal(isWebConfigurationField(key),true,key);
});
test('backup restores never modify or inject out-of-scope BLE fields, even with mismatched types',()=>{
 const make=(kind,body)=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:'+kind+'\n'+body+'\nEND\n'));
 const current=make('working','device.node_name=str:Old\ndevice.ble_enabled=bool:0\nmt_ble.mode=u8:1\nmt_ble.fixed_pin=u32:123456\nmc_ble.pin=u32:123456\nmc_ble.telemetry_environment=u8:3\nmc_ble.future=opaque:keep'),backup=make('backup','device.node_name=str:New\ndevice.ble_enabled=bool:1\nmt_ble.mode=str:wrong\nmt_ble.fixed_pin=u32:999999\nmc_ble.pin=u32:999999\nmc_ble.telemetry_environment=u8:2\nmc_ble.future=opaque:replace\nmc_ble.extra=u8:1');
 const result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);assert.deepEqual(result.changed.map(row=>row.key),['device.node_name']);assert.equal(result.document.get('device.node_name').value,'New');
 for(const row of current.entries().filter(row=>!isWebConfigurationField(row.key)))assert.deepEqual(result.document.get(row.key),row);assert.throws(()=>result.document.get('mc_ble.extra'));
 assert.equal(result.skipped.length,7);for(const {id} of locales)assert.deepEqual(configurationReview(result.document,current,id).map(row=>row.key),['device.node_name']);
 const changed=new TmsDocument(current.serialize());changed.set('mt_ble.fixed_pin',999999);for(const {id} of locales)assert.deepEqual(configurationReview(changed,current,id),[]);
});
test('configuration search has no path back to unsupported BLE controls',()=>{
 const rows=[{key:'mc_ble.pin',label:'PIN',help:'password'},{key:'device.ble_enabled',label:'Bluetooth'},{key:'mt_ble.future',label:'new setting'},{key:'device.node_name',label:'Device name'}];
 assert.deepEqual(findConfigurationFields(rows,'').map(row=>row.key),['device.node_name']);assert.deepEqual(findConfigurationFields(rows,'ble'),[]);assert.deepEqual(findConfigurationFields(rows,'PIN'),[]);assert.deepEqual(findConfigurationFields(rows,'',{changedOnly:true,changes:new Set(['mc_ble.pin'])}),[]);
});
