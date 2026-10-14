import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {moduleNormalizationRule} from '../../site/device/src/meshtastic-module-normalization.js';
import {configModuleText} from '../../site/i18n/config-module-labels.js';
import {configModuleNormalizationText} from '../../site/i18n/config-module-normalization-labels.js';
import {locales} from '../../site/i18n/registry.js';

test('module normalization descriptions correspond exactly to firmware MQTT repair rules',()=>{
 const source=readFileSync(new URL('../../modules/core_phone/include/phone/meshtastic/meshtastic_phone_config_bridge.h',import.meta.url),'utf8');
 const repair=source.slice(source.indexOf('inline void repairLegacyMqttConfig'),source.indexOf('inline void initDefaultModuleConfig'));
 assert.match(repair,/config->has_mqtt = true/);
 assert.match(repair,/config->mqtt\.has_map_report_settings = true/);
 for(const name of ['address','username','password','root']){
  assert.ok(repair.includes(`config->mqtt.${name}[0] == '\\0'`));
  assert.equal(moduleNormalizationRule(['mqtt',name]),'blankMqttValue');
 }
 for(const path of [['mqtt'],['mqtt','map_report_settings']])assert.equal(moduleNormalizationRule(path),'restoredPresence');
 for(const path of [['mqtt','enabled'],['mqtt','map_report_settings','publish_interval_secs'],['serial'],['serial','enabled'],['remote_hardware','available_pins']])assert.equal(moduleNormalizationRule(path),undefined);
});

test('normalization help is included in every module locale without changing staged data',()=>{
 const staged={path:['mqtt','password'],value:''},before=structuredClone(staged);
 for(const {id} of locales){
  const text=configModuleText(id),normalization=configModuleNormalizationText(id);
  for(const key of ['blankMqttValue','restoredPresence'])assert.equal(text[key],normalization[key]);
  assert.ok(text[moduleNormalizationRule(staged.path)].trim());
 }
 assert.deepEqual(staged,before);
 assert.throws(()=>configModuleNormalizationText('unsupported'));
});
