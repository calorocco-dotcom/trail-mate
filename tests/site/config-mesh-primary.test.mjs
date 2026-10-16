import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configMeshChannelText} from '../../site/i18n/config-mesh-channel-labels.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {locales} from '../../site/i18n/registry.js';

test('MeshCore first channel has a fixed enabled state matching core and Preferences admission',()=>{
 const codec=readFileSync(new URL('../../modules/core_sys/src/app/tms_config_codec.cpp',import.meta.url),'utf8');
 const preferences=readFileSync(new URL('../../platform/esp/arduino_common/src/app_config_store.cpp',import.meta.url),'utf8');
 assert.match(codec,/target_->meshcore_config\.meshCoreChannel\(0\)\.enabled = true/);
 assert.match(preferences,/put_bool\(key, slot == 0 \? true : channel\.enabled\)/);
 const record={key:'mc.channel.0.enabled',type:'bool',value:true};
 assert.equal(configurationField(record).readOnly,true);
 assert.throws(()=>validateConfigurationField(record,false),{code:'CONFIG_READ_ONLY'});
 for(const {id} of locales){
  const schema=configurationField(record,configurationPresentation(record.key,id));
  assert.equal(schema.readOnly,true);
  assert.equal(schema.help,configMeshChannelText(id).meshCorePrimaryHelp);
  assert.ok(schema.help.trim());
 }
 for(const key of ['mc.channel.1.enabled','mc.channel.7.enabled','channel.primary_enabled']){
  assert.equal(configurationField({key,type:'bool'}).readOnly,false);
  validateConfigurationField({key,type:'bool'},false);
 }
 validateConfigurationField({key:'mc.channel.0.name',type:'str'},'Camp');
 validateConfigurationField({key:'mc.channel.0.key',type:'b64'},new Uint8Array(16));
});

test('backup import preserves the fixed first-channel state while restoring editable channel fields',()=>{
 const current=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmc.channel.0.enabled=bool:1\nmc.channel.0.name=str:Original\nmc.channel.1.enabled=bool:1\nEND\n'));
 const backup=new TmsDocument(current.serialize({kind:'backup'}));
 backup.set('mc.channel.0.enabled',false);backup.set('mc.channel.0.name','Camp');backup.set('mc.channel.1.enabled',false);
 const result=prepareConfigImport(current,backup);
 assert.deepEqual(result.errors,[]);
 assert.equal(result.document.get('mc.channel.0.enabled').value,true);
 assert.equal(result.document.get('mc.channel.0.name').value,'Camp');
 assert.equal(result.document.get('mc.channel.1.enabled').value,false);
 assert.deepEqual(result.changed.map(item=>item.key).sort(),['mc.channel.0.name','mc.channel.1.enabled']);
 assert.ok(result.skipped.some(item=>item.key==='mc.channel.0.enabled'));
 assert.equal(current.get('mc.channel.0.name').value,'Original');
});
