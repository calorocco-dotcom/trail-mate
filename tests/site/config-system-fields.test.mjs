import test from 'node:test';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {configSystemFieldText,configSystemFieldKeys} from '../../site/i18n/config-system-field-labels.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
test('actual nested system field paths render in all supported languages',()=>{
 for(const {id} of locales){const text=configSystemFieldText(id);assert.equal(Object.keys(text).length,configSystemFieldKeys.length);for(const key of configSystemFieldKeys)assert.ok(text[key]?.trim());
  for(const key of ['ui.audio.speaker_volume','ui.timezone.offset_minutes','chat.auto_reply.text','debug.sd_logs_enabled','power.gauge.design_mah']){const label=configurationField({key,type:'u32'},configurationPresentation(key,id)).label;assert.ok(label.includes(text[key.split('.').at(-1)]));if(!['zh-Hans','zh-Hant-TW','ja'].includes(id))assert.ok(!/[\u3400-\u9fff]/.test(label),`${id}: ${label}`);}
 }
});
test('system input ranges match firmware bounds and timeout choices',()=>{
 for(const [key,type,min,max] of [['ui.display.brightness','i32',0,255],['ui.audio.speaker_volume','i32',0,100],['ui.timezone.offset_minutes','i32',-720,840],['power.gauge.design_mah','u32',1,10000],['power.gauge.full_mah','u32',1,10000],['chat.alerts.message','i32',0,1],['chat.alerts.contact','i32',0,2]]){const record={key,type};assert.deepEqual(configurationField(record).range,[min,max]);validateConfigurationField(record,min);validateConfigurationField(record,max);assert.throws(()=>validateConfigurationField(record,min-1));assert.throws(()=>validateConfigurationField(record,max+1));}
 for(const {id} of locales){const key='ui.screen.timeout_ms',record={key,type:'u32'},schema=configurationField(record,configurationPresentation(key,id));assert.deepEqual(schema.options.map(option=>option.value),[15000,30000,60000,300000]);for(const option of schema.options)validateConfigurationField(record,option.value);assert.throws(()=>validateConfigurationField(record,10000));}
});
