import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationReview} from '../../site/device/src/config-review.js';
import {configReviewText} from '../../site/i18n/config-review-labels.js';
import {locales} from '../../site/i18n/registry.js';
import {updateStructuredModuleField} from '../../site/device/src/meshtastic-module-model.js';
import {replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
import {protobufVarintField} from '../../site/device/src/module-protobuf-wire.js';
const make=lines=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\n'+lines+'END\n'));
test('brightness and volume reviews use percentages in all locales',()=>{
 const before=make('ui.display.brightness=u8:128\nui.audio.speaker_volume=u8:45\n'),after=make('ui.display.brightness=u8:89\nui.audio.speaker_volume=u8:60\n');
 for(const {id} of locales){const rows=configurationReview(after,before,id),percent=new Intl.NumberFormat(id,{style:'percent',maximumFractionDigits:1});assert.equal(rows[0].before,percent.format(128/255));assert.equal(rows[0].after,percent.format(89/255));assert.equal(rows[1].after,percent.format(0.6));}
});
test('change review shows actual values and never exposes passwords or binary payloads',()=>{
 const before=make('device.node_name=str:Before\nwifi.enabled=bool:0\nwifi.profile.0.password=str:oldsecret\nmt_module.chunk.0=b64:b2xkc2VjcmV0\n');
 const after=make('device.node_name=str:After\nwifi.enabled=bool:1\nwifi.profile.0.password=str:newsecret\nmt_module.chunk.0=b64:bmV3c2VjcmV0\n');
 for(const locale of locales){
  const text=configReviewText(locale.id),rows=configurationReview(after,before,locale.id),json=JSON.stringify(rows);
  assert.equal(rows.length,4);assert.equal(rows[0].before,'Before');assert.equal(rows[0].after,'After');
  assert.equal(rows[1].before,text.disabled);assert.equal(rows[1].after,text.enabled);
  assert.equal(rows[2].before,text.secret);assert.equal(rows[2].after,text.secret);
  assert.equal(rows[3].after,text.binary);assert.ok(!json.includes('oldsecret')&&!json.includes('newsecret')&&!json.includes('bmV3c2VjcmV0'));
 }
 assert.deepEqual(configurationReview(before,before,'zh-Hans'),[]);
});
test('structured module review exposes actual fields while masking secrets in every locale',()=>{
 const before=make('mt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:QAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\n');
 let after=updateStructuredModuleField(before,['mqtt','enabled'],true);
 after=updateStructuredModuleField(after,['mqtt','address'],'broker.example');
 after=updateStructuredModuleField(after,['mqtt','password'],'never-show-this-secret');
 after=updateStructuredModuleField(after,['serial','mode'],3);
 for(const locale of locales){
  const rows=configurationReview(after,before,locale.id),text=configReviewText(locale.id),json=JSON.stringify(rows);
  assert.equal(rows.find(row=>row.key==='module.mqtt.address').after,'broker.example');
  assert.equal(rows.find(row=>row.key==='module.mqtt.password').after,text.secret);
  assert.equal(rows.find(row=>row.key==='module.mqtt.enabled').after,text.enabled);
  assert.ok(!rows.some(row=>row.key.startsWith('mt_module.chunk.')));assert.ok(!json.includes('never-show-this-secret'));
  assert.notEqual(rows.find(row=>row.key==='module.serial.mode').after,'3');
 }
 const unknownChanged=replaceMeshtasticModule(before,Uint8Array.from([...protobufVarintField(8,1),...protobufVarintField(99,23)]));
 assert.equal(configurationReview(unknownChanged,before,'zh-Hans').find(row=>row.key==='module.unknown').after,configReviewText('zh-Hans').binary);
});
test('satellite mask review explains the selected systems in every locale',()=>{
 const before=make('gps.sat_mask=u8:141\n'),after=make('gps.sat_mask=u8:143\n');
 for(const locale of locales){const [row]=configurationReview(after,before,locale.id);assert.ok(row.before.includes('GPS')&&row.before.includes('BeiDou'));assert.ok(!row.before.includes('GLONASS'));assert.ok(row.after.includes('GLONASS'));}
});
