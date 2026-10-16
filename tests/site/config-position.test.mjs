import test from 'node:test';import assert from 'node:assert/strict';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {locales} from '../../site/i18n/registry.js';
test('device setting enums reject unsupported position and recording values',()=>{
 for(const [key,values] of [['gps.mode',[0,1,2]],['gps.coord_format',[0,1,2]],['map.coord_system',[0,1,2]],['map.source',[0,1,2]],['map.track_interval',[1,5,10,99]],['map.track_format',[0,1,2]]]){
  const record={key,type:'u8',value:values[0]};assert.deepEqual(configurationField(record).options.map(option=>option.value),values);for(const value of values)validateConfigurationField(record,value);assert.throws(()=>validateConfigurationField(record,100),error=>error.code==='CONFIG_OPTION');
 }
});
test('all eleven locales present position options while retaining numeric wire values',()=>{
 for(const locale of locales){for(const key of ['gps.mode','gps.coord_format','map.coord_system','map.source','map.track_interval','map.track_format']){const field=configurationField({key,type:'u8',value:0},configurationPresentation(key,locale.id));assert.ok(field.options.every(option=>typeof option.label==='string'&&option.label.length));}const interval=configurationField({key:'map.track_interval',type:'u8',value:99},configurationPresentation('map.track_interval',locale.id));assert.equal(interval.options.at(-1).value,99);}
});
