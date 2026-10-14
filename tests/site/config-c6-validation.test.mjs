import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {C6_CONFIG_FIELDS,validateC6Configuration} from '../../site/device/src/config-c6-validation.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {saveConfigDocument} from '../../site/device/src/config-save.js';
import {locales} from '../../site/i18n/registry.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
const make=(kind='working')=>new TmsDocument(new TextEncoder().encode(`TMSET7\nschema.version=u16:7\ndocument.kind=enum:${kind}\n`+Object.entries(C6_CONFIG_FIELDS).map(([name,spec])=>`c6_companion.${name}=${spec.type}:${spec.type==='bool'?0:spec.type==='str'?'':spec.min}`).join('\n')+'\nEND\n'));
test('all C6 persistence keys fit NVS and resolve without collisions',()=>{
 const source=fs.readFileSync(new URL('../../platform/esp/common/include/platform/esp/common/settings_store_impl.h',import.meta.url),'utf8');
 const table=source.match(/constexpr StorageKeyAlias kStorageKeyAliases\[\] = \{([\s\S]*?)\n\};/)[1];
 const aliases=new Map([...table.matchAll(/\{"([^"]+)", "([^"]+)"\}/g)].map(match=>[match[1],match[2]]));
 const keys=Object.keys(C6_CONFIG_FIELDS).map(key=>aliases.get(key)??key);assert.equal(new Set(keys).size,14);for(const key of keys)assert.ok(key.length<=15,key);
});
test('all 11 locales name every C6 field and input constraints match the contract',()=>{
 for(const locale of locales)for(const record of make().entries().filter(row=>row.key.startsWith('c6_companion.'))){const presentation=configurationPresentation(record.key,locale.id),schema=configurationField(record,presentation);assert.ok(schema.label&&!schema.label.includes('c6_companion')&&!schema.label.includes(record.key.split('.').at(-1)));}
 const document=make(),password=document.get('c6_companion.wifi_ap_password');assert.equal(configurationField(password).secret,true);assert.equal(configurationField(password).maxBytes,63);
 assert.throws(()=>validateConfigurationField(password,'山'.repeat(22)),{code:'CONFIG_UTF8_LIMIT'});
 assert.throws(()=>validateConfigurationField(document.get('c6_companion.wifi_ap_channel'),0),{code:'CONFIG_RANGE'});
 validateConfigurationField(document.get('c6_companion.espnow_channel'),0);
});
test('optional board block accepts complete values and counts UTF-8 bytes',()=>{
 const document=make();validateC6Configuration(document);document.set('c6_companion.wifi_sta_ssid','山'.repeat(10)+'x');validateC6Configuration(document);
 document.set('c6_companion.wifi_sta_ssid','山'.repeat(11));assert.throws(()=>validateC6Configuration(document),{code:'C6_CONFIG_INVALID'});
 const empty=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nEND\n'));validateC6Configuration(empty);
});
test('partial blocks, wrong types, channel and interval boundaries are rejected',()=>{
 const records=make().entries();assert.throws(()=>validateC6Configuration({entries:()=>records.slice(0,-1)}),{code:'C6_CONFIG_INVALID'});
 for(const [key,value] of [['wifi_ap_channel',0],['espnow_channel',15],['espnow_beacon_interval_ms',0],['wifi_sta_password','x'.repeat(64)]]){const document=make();document.set('c6_companion.'+key,value);assert.throws(()=>validateC6Configuration(document),{code:'C6_CONFIG_INVALID'});}
 assert.throws(()=>validateC6Configuration({entries:()=>records.map(record=>record.key==='c6_companion.wifi_enabled'?{...record,type:'u8',value:1}:record)}),{code:'C6_CONFIG_INVALID'});
});
test('invalid backup rolls back the whole candidate and sends no upload',async()=>{
 const current=make(),backup=make('backup');backup.set('c6_companion.wifi_ap_channel',0);backup.set('c6_companion.wifi_ap_ssid','New');
 const result=prepareConfigImport(current,backup);assert.equal(result.errors.length,1);assert.equal(result.changed.length,0);assert.equal(result.document.get('c6_companion.wifi_ap_ssid').value,'');
 let calls=0;await assert.rejects(saveConfigDocument({request:()=>{calls++;}},backup,new Uint8Array(32),{id:new Uint8Array(16).fill(1)}),{code:'C6_CONFIG_INVALID'});assert.equal(calls,0);
});
test('enabled STA/AP requires its SSID while disabled credentials remain editable',()=>{
 for(const mode of ['sta','ap']){
  const document=make();document.set(`c6_companion.wifi_${mode}_enabled`,true);validateC6Configuration(document);
  document.set('c6_companion.wifi_enabled',true);assert.throws(()=>validateC6Configuration(document),{code:'C6_CONFIG_SSID_REQUIRED',field:`c6_companion.wifi_${mode}_ssid`});
  document.set(`c6_companion.wifi_${mode}_ssid`,'Camp');validateC6Configuration(document);
 }
});
test('active AP allows open mode or 8–63 byte WPA2 passwords',()=>{
 const document=make();document.set('c6_companion.wifi_ap_ssid','Camp');document.set('c6_companion.wifi_enabled',true);document.set('c6_companion.wifi_ap_enabled',true);
 validateC6Configuration(document);document.set('c6_companion.wifi_ap_password','1234567');assert.throws(()=>validateC6Configuration(document),{code:'C6_CONFIG_AP_PASSWORD'});
 document.set('c6_companion.wifi_ap_password','12345678');validateC6Configuration(document);
 document.set('c6_companion.wifi_ap_password','山山山');validateC6Configuration(document);
 document.set('c6_companion.wifi_ap_enabled',false);document.set('c6_companion.wifi_ap_password','short');validateC6Configuration(document);
});
test('shared Wi-Fi forcing C6 on also activates AP validation',()=>{
 const document=make();document.set('c6_companion.wifi_sta_ssid','Camp');document.set('c6_companion.wifi_ap_enabled',true);document.set('c6_companion.wifi_ap_ssid','Camp AP');document.set('c6_companion.wifi_ap_password','short');
 validateC6Configuration(document);
 const text=new TextDecoder().decode(document.serialize({kind:'working'})).replace('END\n','wifi.enabled=bool:1\nEND\n');
 const effective=new TmsDocument(new TextEncoder().encode(text));assert.throws(()=>validateC6Configuration(effective),{code:'C6_CONFIG_AP_PASSWORD'});
 effective.set('c6_companion.wifi_ap_password','12345678');validateC6Configuration(effective);
});
