import test from 'node:test';
import assert from 'node:assert/strict';
import {configurationSections} from '../../site/device/src/config-sections.js';
import {configSectionText} from '../../site/i18n/config-section-labels.js';
import {locales} from '../../site/i18n/registry.js';
test('settings sections retain every record and divide receiver setup from everyday positioning',()=>{
 const records=['gps.enabled','gps.init_baud','gps.motion_idle_ms','gps.external_nmea_hz','gps.mode','mt.tx_power','mt.mqtt.host'].map(key=>({key,type:'u32',value:1}));
 for(const locale of locales){const sections=configurationSections(records,locale.id),text=configSectionText(locale.id);
  assert.equal(sections.length,6);assert.equal(sections[0].label,text.position);assert.equal(sections[1].label,text.startup);
  assert.deepEqual(sections[0].records.map(row=>row.key),['gps.enabled','gps.mode']);
  assert.equal(sections.flatMap(section=>section.records).length,records.length);assert.equal(new Set(sections.flatMap(section=>section.records.map(row=>row.key))).size,records.length);
 }
});
