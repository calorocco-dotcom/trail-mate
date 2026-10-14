import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';

test('modem text controls reserve the firmware NUL and enforce UTF-8 capacity',()=>{
 const header=readFileSync(new URL('../../modules/core_sys/include/platform/ui/a7682e_cellular_runtime.h',import.meta.url),'utf8');
 const members=[...header.matchAll(/char (apn\w*|smsc|smtp_\w+)\[(\d+)\]/g)];
 assert.equal(members.length,9);
 for(const [,name,capacity] of members){
  const record={key:'cellular.'+name,type:'str'},limit=Number(capacity)-1;
  assert.equal(configurationField(record).maxBytes,limit);
  validateConfigurationField(record,'a'.repeat(limit));
  assert.throws(()=>validateConfigurationField(record,'a'.repeat(limit+1)),{code:'CONFIG_UTF8_LIMIT'});
  const unicode='界'.repeat(Math.floor(limit/3))+'a'.repeat(limit%3);
  validateConfigurationField(record,unicode);
  assert.throws(()=>validateConfigurationField(record,unicode+'界'),{code:'CONFIG_UTF8_LIMIT'});
 }
});

test('cellular controls reject the numeric values rejected by firmware admission',()=>{
 const source=readFileSync(new URL('../../platform/esp/arduino_common/src/app_config_tms_settings_extension.cpp',import.meta.url),'utf8');
 const validator=source.match(/bool valid_cellular_config\([^]*?\n\}/)?.[0];
 assert.ok(validator);
 for(const name of ['speaker_gain','microphone_gain']){
  assert.match(validator,new RegExp('config\\.'+name+' <= 15U'));
  const record={key:'cellular.'+name,type:'u8'};
  for(const value of [0,15])validateConfigurationField(record,value);
  assert.throws(()=>validateConfigurationField(record,16),{code:'CONFIG_RANGE'});
 }
 assert.match(validator,/config\.smtp_security <= 2U/);
 validateConfigurationField({key:'cellular.smtp_security',type:'u8'},2);
 assert.throws(()=>validateConfigurationField({key:'cellular.smtp_security',type:'u8'},3),{code:'CONFIG_RANGE'});
 const record={key:'cellular.smtp_port',type:'u16'};
 for(const value of [25,465,587]){assert.match(validator,new RegExp('config\\.smtp_port == '+value+'U'));validateConfigurationField(record,value);}
 for(const value of [2525,1,65535])assert.throws(()=>validateConfigurationField(record,value),{code:'CONFIG_OPTION'});
 assert.equal(configurationField({key:'rt.wifi_port',type:'u16'}).options,undefined);
 validateConfigurationField({key:'rt.wifi_port',type:'u16'},2525);
});
