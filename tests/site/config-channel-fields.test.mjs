import test from 'node:test';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {configChannelFieldText,configChannelFieldKeys} from '../../site/i18n/config-channel-field-labels.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';import {configurationField} from '../../site/device/src/config-schema.js';
test('channel, MQTT, Bluetooth and document labels cover all supported languages',()=>{
 for(const {id} of locales){const text=configChannelFieldText(id);assert.equal(Object.keys(text).length,configChannelFieldKeys.length);for(const value of Object.values(text))assert.ok(value.trim());
 for(const key of ['channel.primary_has_module','mt.mqtt.username','mt_ble.fixed_pin','mc_ble.telemetry_environment','mt_module.chunk.0','schema.version','document.kind','network.duty_cycle']){const result=configurationField({key,type:'u8'},configurationPresentation(key,id));if(!['zh-Hans','zh-Hant-TW','ja'].includes(id))assert.ok(!/[\u3400-\u9fff]/.test(result.label),`${id}: ${key}`);}
 }
});
test('presentation does not change read-only metadata or secret inputs',()=>{
 for(const {id} of locales)for(const key of ['schema.version','document.kind','mt_ble.fixed_pin','mc_ble.pin','mt.mqtt.password']){const record={key,type:'u32'},actual=configurationField(record,configurationPresentation(key,id)),baseline=configurationField(record);assert.equal(actual.readOnly,baseline.readOnly);assert.equal(actual.secret,baseline.secret);assert.deepEqual(actual.range,baseline.range);}
});
