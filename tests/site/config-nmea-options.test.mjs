import test from 'node:test';import assert from 'node:assert/strict';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {locales} from '../../site/i18n/registry.js';
test('external NMEA selectors match current device preset tables across languages',()=>{
 for(const locale of locales){const hz=configurationField({key:'gps.external_nmea_hz',type:'u8'},configurationPresentation('gps.external_nmea_hz',locale.id)),sentences=configurationField({key:'gps.external_nmea_mask',type:'u8'},configurationPresentation('gps.external_nmea_mask',locale.id));
  assert.deepEqual(hz.options.map(row=>row.value),[0,1,5]);assert.ok(hz.options[0].label.length);assert.deepEqual(sentences.options.map(row=>row.value),[0,1,2]);assert.equal(sentences.options[2].label,'GGA + RMC');
 }
 validateConfigurationField({key:'gps.external_nmea_hz',type:'u8'},5);assert.throws(()=>validateConfigurationField({key:'gps.external_nmea_hz',type:'u8'},2));assert.throws(()=>validateConfigurationField({key:'gps.external_nmea_mask',type:'u8'},4));
});
