import test from 'node:test';import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationFieldAvailable} from '../../site/device/src/config-dependencies.js';
test('switching preset preserves custom modulation values and changes only availability',()=>{
 const document=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmt.use_preset=bool:1\nmt.modem_preset=u8:0\nmt.bandwidth_khz=f32:125\nmt.spread_factor=u8:9\nmt.coding_rate=u8:5\nEND\n'));
 const values=['mt.bandwidth_khz','mt.spread_factor','mt.coding_rate'].map(key=>document.get(key).value);
 for(const key of ['mt.bandwidth_khz','mt.spread_factor','mt.coding_rate'])assert.equal(configurationFieldAvailable(document,key),false);assert.equal(configurationFieldAvailable(document,'mt.modem_preset'),true);
 document.set('mt.use_preset',false);for(const key of ['mt.bandwidth_khz','mt.spread_factor','mt.coding_rate'])assert.equal(configurationFieldAvailable(document,key),true);assert.equal(configurationFieldAvailable(document,'mt.modem_preset'),false);
 assert.deepEqual(['mt.bandwidth_khz','mt.spread_factor','mt.coding_rate'].map(key=>document.get(key).value),values);assert.equal(configurationFieldAvailable(document,'mt.tx_power'),true);
});
test('absent or unknown dependency cannot silently lock unrelated configurations',()=>{
 const missing=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nEND\n'));assert.equal(configurationFieldAvailable(missing,'gps.enabled'),true);
 assert.equal(configurationFieldAvailable({get:()=>undefined},'mt.bandwidth_khz'),true);assert.equal(configurationFieldAvailable({get:()=>({type:'opaque',value:undefined})},'mt.bandwidth_khz'),true);
});
