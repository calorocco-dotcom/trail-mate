import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {collectLegacyWrites} from '../../scripts/audit_app_config_preferences.mjs';
const store=await readFile('platform/esp/arduino_common/src/app_config_store.cpp','utf8'),codec=await readFile('modules/core_sys/src/app/tms_config_codec.cpp','utf8');
test('actual legacy save wrappers have projections, including dynamic slots and compatibility metadata',()=>{
 const rows=collectLegacyWrites(store,codec);assert.equal(rows.length,138);assert.equal(rows.filter(row=>!row.fields.length).length,0);
 for(const key of ['mc_ch{slot}_en','mc_ch{slot}_name','mc_ch{slot}_key','pri_key_len','sec_key_len','rn_freq','ext_nmea_sent','route_path'])assert.ok(rows.some(row=>row.key===key&&row.fields.length));
 assert.deepEqual(rows.find(row=>row.key==='mt_mqtt_pass').fields,['mt.mqtt.password']);
 assert.deepEqual(rows.find(row=>row.key==='mc_mqtt_pass').fields,['mc.mqtt.password']);
});
test('an added persisted setting without TMS projection stays visible as unresolved',()=>{
 const changed=store.replace('put_bool("route_enabled", route_enabled);','put_bool("new_setting", config.new_setting);\nput_bool("route_enabled", route_enabled);');
 const missing=collectLegacyWrites(changed,codec).filter(row=>!row.fields.length);
 assert.deepEqual(missing.map(row=>row.key),['new_setting']);
});
test('removing a production writer projection exposes its saved members',()=>{
 const changed=codec.replace('encoder.boolean("route.enabled", config.route_enabled)','encoder.boolean("route.enabled", config.different_member)');
 assert.ok(collectLegacyWrites(store,changed).some(row=>row.key==='route_enabled'&&!row.fields.length));
});
test('comment and string decoys do not become save calls and values never enter inventory',()=>{
 const changed=store.replace('put_bool("route_enabled", route_enabled);','// put_string("fake", config.fake);\nconst char* decoy = "put_string(\\\"fake\\\", config.fake)";\nput_string("value_probe", "SECRET_VALUE_MUST_NOT_APPEAR");\nput_bool("route_enabled", route_enabled);');
 const rows=collectLegacyWrites(changed,codec);assert.equal(rows.some(row=>row.key==='fake'),false);assert.equal(rows.length,139);assert.equal(JSON.stringify(rows).includes('SECRET_VALUE_MUST_NOT_APPEAR'),false);
});
