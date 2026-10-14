import test from 'node:test';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {configRadioFieldText,configRadioFieldKeys} from '../../site/i18n/config-radio-field-labels.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configurationField} from '../../site/device/src/config-schema.js';
test('radio and policy labels have every language without Chinese fallback',()=>{
 for(const {id} of locales){const labels=configRadioFieldText(id);assert.equal(Object.keys(labels).length,configRadioFieldKeys.length);for(const value of Object.values(labels))assert.ok(value.trim());
  for(const key of ['mt.bandwidth_khz','rt.tx_power','mt.primary_psk','rt.secondary_channel_name','mc.channel.0.key','mc.airtime_factor','policy.ack_squad','protocol.active']){const result=configurationField({key,type:'u8'},configurationPresentation(key,id));assert.ok(result.label);if(!['zh-Hans','zh-Hant-TW','ja'].includes(id))assert.ok(!/[\u3400-\u9fff]/.test(result.label),`${id}: ${key}`);}
 }
});
test('presentation preserves radio option values, secret detection and input ranges',()=>{
 for(const {id} of locales)for(const key of ['mt.modem_preset','protocol.active','rt.primary_psk','mc.channel.0.key','mt.tx_power']){const record={key,type:'u8'},baseline=configurationField(record),localized=configurationField(record,configurationPresentation(key,id));assert.deepEqual(localized.options?.map(option=>option.value),baseline.options?.map(option=>option.value));assert.equal(localized.secret,baseline.secret);assert.deepEqual(localized.range,baseline.range);}
});
