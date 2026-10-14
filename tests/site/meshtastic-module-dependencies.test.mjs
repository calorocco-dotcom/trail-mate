import test from 'node:test';import assert from 'node:assert/strict';
import {readStructuredModule} from '../../site/device/src/meshtastic-module-model.js';
import {protobufBytesField,protobufVarintField} from '../../site/device/src/module-protobuf-wire.js';
import {moduleFieldAvailable} from '../../site/device/src/meshtastic-module-dependencies.js';
import {configModuleDependencyText} from '../../site/i18n/config-module-dependency-labels.js';
test('PWM disables exactly the ignored notification controls without modifying bytes',()=>{
 const bytes=protobufBytesField(3,Uint8Array.from([...protobufVarintField(2,1234),...protobufVarintField(3,13),...protobufVarintField(4,1),...protobufVarintField(7,1)]));
 const before=bytes.slice(),model=readStructuredModule(bytes);
 for(const field of ['output','output_ms','active'])assert.equal(moduleFieldAvailable(model,['external_notification',field]),false);
 for(const path of [['external_notification','enabled'],['external_notification','use_pwm'],['external_notification','output_vibra'],['serial','rxd'],['audio','ptt_pin']])assert.equal(moduleFieldAvailable(model,path),true);
 assert.deepEqual(bytes,before);
 for(const field of ['output','output_ms','active'])assert.equal(moduleFieldAvailable(readStructuredModule(protobufBytesField(3,protobufVarintField(7,0))),['external_notification',field]),true);
 assert.equal(moduleFieldAvailable(readStructuredModule(new Uint8Array()),['external_notification','output']),true);
});
test('retained notification parameter explanation covers eleven languages',()=>{
 for(const locale of ['en','zh-Hans','zh-Hant-TW','ja','ko','ar','ru','de','es','fr','it'])assert.ok(configModuleDependencyText(locale).includes('PWM'));
});
