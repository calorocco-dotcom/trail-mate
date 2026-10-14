import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs/promises';
import {MESHCORE_REGION_PRESETS,MESHCORE_PRESET_FIELDS,meshCoreRegionOptions} from '../../site/device/src/meshcore-region-presets.js';
import {setConfigurationRadioInput} from '../../site/device/src/config-radio-input.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';import {locales} from '../../site/i18n/registry.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';import {configurationReview} from '../../site/device/src/config-review.js';
const source=await fs.readFile('modules/core_chat/src/infra/meshcore/mc_region_presets.cpp','utf8');
const make=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmc.region_preset=u8:0\nmc.frequency_mhz=f32:869.525\nmc.bandwidth_khz=f32:250\nmc.spread_factor=u8:11\nmc.coding_rate=u8:5\nmc.base.tx_power=i8:20\nvendor.future=opaque:preserve\nEND\n'));
test('all 17 web presets agree with the actual firmware table and all locales name them',()=>{
 const rows=[...source.matchAll(/\{(\d+), "[^"]+", ([\d.]+)f, ([\d.]+)f, (\d+), (\d+), (\d+)\}/g)].map(match=>match.slice(1).map(Number));
 assert.deepEqual(MESHCORE_REGION_PRESETS.map(row=>[row.id,row.frequency,row.bandwidth,row.sf,row.cr,row.power]),rows);
 for(const {id} of locales){const options=meshCoreRegionOptions(id);assert.equal(options.length,18);assert.ok(options.every(row=>row.label&&!/^\d+$/.test(row.label)));assert.deepEqual(options.map(row=>row.value),Array.from({length:18},(_,index)=>index));}
});
test('preset edits are atomic, manual radio edits select Custom, power stays independent',()=>{
 for(const preset of MESHCORE_REGION_PRESETS){const document=make();setConfigurationRadioInput(document,'mc.region_preset',preset.id);for(const [key,name] of Object.entries(MESHCORE_PRESET_FIELDS))assert.equal(document.get(key).value,preset[name]);assert.equal(document.get('vendor.future').raw,'preserve');}
 const document=make();setConfigurationRadioInput(document,'mc.region_preset',17);setConfigurationRadioInput(document,'mc.base.tx_power',8);assert.equal(document.get('mc.region_preset').value,17);
 setConfigurationRadioInput(document,'mc.frequency_mhz',495.2);assert.equal(document.get('mc.region_preset').value,17);setConfigurationRadioInput(document,'mc.frequency_mhz',495.3);assert.equal(document.get('mc.region_preset').value,0);
 const bytes=document.serialize();assert.throws(()=>setConfigurationRadioInput(document,'mc.region_preset',99));assert.deepEqual(document.serialize(),bytes);
 const emitted=make();emitted.set('mc.region_preset',17);emitted.set('mc.frequency_mhz',495.200012);setConfigurationRadioInput(emitted,'mc.frequency_mhz',495.2);assert.equal(emitted.get('mc.region_preset').value,17);
 const partial=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmc.region_preset=u8:0\nEND\n')),original=partial.serialize();assert.throws(()=>setConfigurationRadioInput(partial,'mc.region_preset',17));assert.deepEqual(partial.serialize(),original);
});
test('backup import preserves independently stored values and review uses channel names of each snapshot',()=>{
 const current=make(),backup=make();backup.set('mc.region_preset',17);backup.set('mc.frequency_mhz',496.25);backup.set('mc.base.tx_power',7);
 const imported=prepareConfigImport(current,new TmsDocument(backup.serialize({kind:'backup'})));assert.deepEqual(imported.errors,[]);assert.equal(imported.document.get('mc.region_preset').value,17);assert.equal(imported.document.get('mc.frequency_mhz').value,496.25);assert.equal(imported.document.get('mc.base.tx_power').value,7);
 const before=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nprotocol.active=u8:1\nchat.channel=u8:0\nmt.primary_channel_name=str:Camp\nmt.secondary_channel_name=str:Rescue\nEND\n')),after=new TmsDocument(before.serialize());after.set('chat.channel',1);after.set('mt.secondary_channel_name','New rescue');const row=configurationReview(after,before,'zh-Hans').find(row=>row.key==='chat.channel');assert.ok(row.before.includes('Camp'));assert.ok(row.after.includes('New rescue'));
});
