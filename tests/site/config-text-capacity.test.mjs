import test from 'node:test';import assert from 'node:assert/strict';
import {validateConfigurationField} from '../../site/device/src/config-schema.js';
test('device name limits count UTF-8 bytes rather than visible characters',()=>{
 validateConfigurationField({key:'device.node_name',type:'str'},'界'.repeat(10)+'a');
 assert.throws(()=>validateConfigurationField({key:'device.node_name',type:'str'},'界'.repeat(11)),error=>error.code==='CONFIG_UTF8_LIMIT'&&error.params.max===31);
 validateConfigurationField({key:'device.short_name',type:'str'},'界'.repeat(5));
 assert.throws(()=>validateConfigurationField({key:'device.short_name',type:'str'},'界'.repeat(6)),error=>error.code==='CONFIG_UTF8_LIMIT'&&error.params.max===15);
});
test('MQTT, APRS and route text fit the firmware string storage',()=>{
 for(const key of ['mt.mqtt.host','mt.mqtt.root','mt.mqtt.username','mt.mqtt.password','aprs.path']){validateConfigurationField({key,type:'str'},'界'.repeat(21));assert.throws(()=>validateConfigurationField({key,type:'str'},'界'.repeat(22)),error=>error.code==='CONFIG_UTF8_LIMIT');}
 for(const key of ['aprs.igate_callsign','aprs.tocall','aprs.self_callsign'])assert.throws(()=>validateConfigurationField({key,type:'str'},'a'.repeat(16)),error=>error.code==='CONFIG_UTF8_LIMIT');
 validateConfigurationField({key:'route.path',type:'str'},'界'.repeat(31));assert.throws(()=>validateConfigurationField({key:'route.path',type:'str'},'界'.repeat(32)),error=>error.code==='CONFIG_UTF8_LIMIT');
});
