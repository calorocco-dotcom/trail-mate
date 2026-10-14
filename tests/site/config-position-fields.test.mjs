import test from 'node:test';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {configPositionFieldText,configPositionFieldKeys} from '../../site/i18n/config-position-field-labels.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configurationField} from '../../site/device/src/config-schema.js';
test('position field labels cover all supported languages without Chinese fallback',()=>{
 for(const {id} of locales){const text=configPositionFieldText(id);assert.equal(Object.keys(text).length,26);for(const key of configPositionFieldKeys)assert.ok(text[key]?.trim());
  for(const key of ['gps.init_gnss_policy','gps.external_nmea_mask','map.coord_system','map.track_interval','route.path']){const label=configurationField({key,type:'u8'},configurationPresentation(key,id)).label;assert.ok(label.includes(text[key.split('.').at(-1)]));if(!['zh-Hans','zh-Hant-TW','ja'].includes(id))assert.ok(!/[\u3400-\u9fff]/.test(label),`${id}: ${label}`);}
 }
 assert.throws(()=>configPositionFieldText('unknown'));
});
test('localized presentation preserves position options and input constraints',()=>{
 const record={key:'map.track_interval',type:'u8'};
 const source=configurationField(record,configurationPresentation(record.key,'zh-Hans'));
 for(const {id} of locales){const result=configurationField(record,configurationPresentation(record.key,id));assert.deepEqual(result.options.map(option=>option.value),source.options.map(option=>option.value));assert.deepEqual(result.range,source.range);assert.equal(configurationField({key:'route.path',type:'str'},configurationPresentation('route.path',id)).maxBytes,95);}
});
