import test from 'node:test';
import assert from 'node:assert/strict';
import {MODULE_SCHEMA} from '../../site/device/src/meshtastic-module-schema.js';
import {configModuleText,configModuleFieldLabel} from '../../site/i18n/config-module-labels.js';
test('module action copy covers eleven locales without missing interpolation values',()=>{
 for(const locale of ['en','zh-Hans','zh-Hant-TW','ja','ko','ar','ru','de','es','fr','it']){
  const text=configModuleText(locale);for(const value of Object.values(text))assert.equal(typeof value,'string');
  assert.ok(text.byteLimit.includes('{count}'));assert.ok(text.count.includes('{capacity}'));assert.ok(text.unknownOption.includes('{value}'));
 }
});
test('every current module field and enum has dedicated terminology in all eleven locales',()=>{
 let count=0;
 for(const message of Object.values(MODULE_SCHEMA.messages))for(const field of message.fields){
  count++;for(const locale of ['en','zh-Hans','zh-Hant-TW','ja','ko','ar','ru','de','es','fr','it']){
   assert.ok(configModuleFieldLabel([field.name],locale));
   for(const option of field.options??[])assert.ok(configModuleFieldLabel([field.name,option.name],locale,'option'));
  }
 }
 assert.equal(count,119);
});
