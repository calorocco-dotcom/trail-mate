import test from 'node:test';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';import {configBluetoothText} from '../../site/i18n/config-bluetooth-labels.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configBluetoothIssues} from '../../site/device/src/config-bluetooth-issues.js';import {formatConfigCollectionIssue} from '../../site/i18n/config-collection-issue-labels.js';
test('Bluetooth pairing mode options follow firmware IDs in eleven locales',()=>{
 for(const {id} of locales){const text=configBluetoothText(id),key='mt_ble.mode',options=configurationField({key,type:'u8'},configurationPresentation(key,id)).options;assert.deepEqual(options.map(value=>value.value),[0,1,2]);assert.deepEqual(options.map(value=>value.label),[text.randomPin,text.fixedPin,text.noPin]);}
 assert.throws(()=>validateConfigurationField({key:'mt_ble.mode',type:'u8'},3));
});
test('PIN constraints reject intermediate values and retain the zero sentinel',()=>{
 for(const key of ['mt_ble.fixed_pin','mc_ble.pin']){const record={key,type:'u32'};for(const value of [0,100000,999999])validateConfigurationField(record,value);for(const value of [1,99999,1000000,-1,1.5])assert.throws(()=>validateConfigurationField(record,value));assert.equal(configurationField(record).secret,true);}
 for(const key of ['mc_ble.telemetry_base','mc_ble.telemetry_location','mc_ble.telemetry_environment']){validateConfigurationField({key,type:'u8'},3);assert.throws(()=>validateConfigurationField({key,type:'u8'},4));}
});
test('no PIN cross-field issue is localized and never rewrites stored PIN',()=>{
 const mode={type:'u8',value:2},pin={type:'u32',value:123456},document={get:key=>key==='mt_ble.mode'?mode:pin};
 const issues=configBluetoothIssues(document);assert.equal(issues.length,1);assert.equal(pin.value,123456);
 for(const {id} of locales)assert.equal(formatConfigCollectionIssue(issues[0],id),configBluetoothText(id).BLE_NO_PIN_REQUIRES_ZERO);
 pin.value=0;assert.deepEqual(configBluetoothIssues(document),[]);assert.deepEqual(configBluetoothIssues({get(){throw Error('absent');}}),[]);
});
