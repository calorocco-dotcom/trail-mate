import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {locales} from '../../site/i18n/registry.js';
const files=['modules/core_sys/src/app/tms_config_codec.cpp','platform/esp/arduino_common/src/app_config_tms_settings_extension.cpp','platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp'];
test('literal firmware writer fields retain actual storage types and localized presentation',async()=>{
 const records=new Map(),types={boolean:'bool',text:'str',blob:'b64',enumeration:'enum'};
 for(const file of files){const source=await readFile(new URL('../../'+file,import.meta.url),'utf8');
  for(const match of source.matchAll(/(?:encoder|writer)\.(boolean|u8|i8|u16|u32|i32|f32|text|blob|enumeration)\(\s*"([a-z0-9_.]+)"/g)){
   const type=types[match[1]]??match[1],key=match[2],previous=records.get(key);assert.ok(!previous||previous===type,`${key}: conflicting writer types`);records.set(key,type);
  }
  for(const match of source.matchAll(/write_(i32|u32|bool)_setting\(\s*writer,\s*"([a-z0-9_.]+)"/g)){const type=match[1],key=match[2],previous=records.get(key);assert.ok(!previous||previous===type,`${key}: conflicting helper types`);records.set(key,type);}
 }
 assert.ok(records.size>=100,'Source scan did not cover expected literal writer fields');
 for(const [key,type] of records)for(const locale of locales){
  const field=configurationField({key,type},configurationPresentation(key,locale.id));assert.equal(field.type,type);assert.ok(field.label.trim(),`${locale.id}: ${key}`);
  if(['u8','i8','u16','u32','i32'].includes(type))assert.ok(Array.isArray(field.range)&&field.range.length===2,`${key}: integer bounds missing`);
  if(type==='f32')assert.throws(()=>validateConfigurationField({key,type},Infinity),error=>error.code==='CONFIG_FLOAT',`${key}: float validity missing`);
  if(!['zh-Hans','zh-Hant-TW','ja'].includes(locale.id))assert.ok(!/[\u3400-\u9fff]/.test(field.label),`${locale.id}: ${key}`);
 }
 assert.equal(records.get('gps.external_nmea_mask'),'u8');assert.equal(records.get('gps.sat_mask'),'u8');assert.equal(records.get('gps.enabled'),'bool');assert.equal(records.get('route.path'),'str');
 assert.equal(records.get('ui.screen.timeout_ms'),'u32');assert.equal(records.get('ui.timezone.offset_minutes'),'i32');assert.equal(records.get('ui.haptics.vibration_enabled'),'bool');assert.equal(records.get('power.gauge.full_mah'),'u32');
});
