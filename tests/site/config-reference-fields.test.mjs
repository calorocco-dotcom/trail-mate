import test from 'node:test';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {configurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
test('Reticulum group references and device Bluetooth labels have every supported locale',()=>{
 for(const {id} of locales)for(const key of ['rt.group.0.name','rt.group.0.enabled','rt.group.0.identity_valid','rt.group.0.identity_hash','rt.group.0.destination_hash','device.ble_enabled']){
  const schema=configurationField({key,type:'str'},configurationPresentation(key,id));assert.ok(schema.label);if(!['zh-Hans','zh-Hant-TW','ja'].includes(id))assert.ok(!/[\u3400-\u9fff]/.test(schema.label),`${id}: ${key}`);
 }
});
