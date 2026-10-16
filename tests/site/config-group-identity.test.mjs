import test from 'node:test';import assert from 'node:assert/strict';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configBinaryText} from '../../site/i18n/config-binary-labels.js';
import {locales} from '../../site/i18n/registry.js';
test('group identity references remain editable but require exactly 16 decoded bytes',()=>{
 for(const field of ['destination_hash','identity_hash']){const record={key:'rt.group.0.'+field,type:'b64',value:new Uint8Array(16)};assert.equal(configurationField(record).readOnly,false);validateConfigurationField(record,new Uint8Array(16));for(const length of [0,15,17,32])assert.throws(()=>validateConfigurationField(record,new Uint8Array(length)),error=>error.code==='CONFIG_BINARY_LENGTH'&&error.params.bytes===16);}
});
test('binary-length diagnostics are supplied for every supported locale',()=>{for(const locale of locales)assert.ok(configBinaryText(locale.id).CONFIG_BINARY_LENGTH.includes('{bytes}'));});
