import test from 'node:test';import assert from 'node:assert/strict';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
test('Meshtastic presets use actual wire IDs and retain the retired value as unknown',()=>{
 const record={key:'mt.modem_preset',type:'u8',value:0},options=configurationField(record).options;
 assert.equal(options.find(option=>option.label==='LongTurbo').value,9);assert.equal(options.find(option=>option.label==='ShortTurbo').value,8);assert.equal(options.find(option=>option.label==='LongModerate').value,7);
 for(const option of options)validateConfigurationField(record,option.value);
 assert.throws(()=>validateConfigurationField(record,2),error=>error.code==='CONFIG_OPTION');assert.throws(()=>validateConfigurationField(record,10),error=>error.code==='CONFIG_OPTION');
});
