import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configBinaryText} from '../../site/i18n/config-binary-labels.js';
import {locales} from '../../site/i18n/registry.js';
const header=readFileSync(new URL('../../modules/core_chat/include/chat/domain/chat_types.h',import.meta.url),'utf8');

test('mesh text capacities agree with firmware storage, including Unicode boundaries',()=>{
 const checks=[];
 for(const field of ['Host','Root','Username','Password']){
  const limit=Number(header.match(new RegExp('kMeshCoreMqtt'+field+'MaxLen = (\\d+)'))?.[1]);
  assert.equal(limit,63);checks.push(['mc.mqtt.'+field.toLowerCase(),limit]);
 }
 for(const field of ['primary','secondary']){
  const capacity=Number(header.match(new RegExp('char '+field+'_channel_name\\[(\\d+)\\]'))?.[1]);
  for(const prefix of ['mt','rt','mc.base'])checks.push([prefix+'.'+field+'_channel_name',capacity-1]);
 }
 checks.push(['mc.channel.7.name',Number(header.match(/kMeshCoreChannelNameMaxLen = (\d+)/)?.[1])-1]);
 checks.push(['rt.group.1.name',Number(header.match(/kReticulumGroupNameMaxLen = (\d+)/)?.[1])-1]);
 checks.push(['rt.wifi_host',Number(header.match(/kReticulumGatewayHostMaxLen = (\d+)/)?.[1])]);
 for(const [key,limit] of checks){
  assert.ok(Number.isSafeInteger(limit)&&limit>0,key);
  const record={key,type:'str'};assert.equal(configurationField(record).maxBytes,limit);
  const exact='界'.repeat(Math.floor(limit/3))+'a'.repeat(limit%3);
  validateConfigurationField(record,exact);
  assert.throws(()=>validateConfigurationField(record,exact+'a'),{code:'CONFIG_UTF8_LIMIT'});
 }
});

test('base channel key admission matches core codec without constraining unknown binary fields',()=>{
 const source=readFileSync(new URL('../../modules/core_sys/src/app/tms_config_codec.cpp',import.meta.url),'utf8');
 assert.match(source,/decoded != 0U && decoded != chat::kMeshtasticChannelKeyDefaultLen &&\s*decoded != chat::kMeshtasticChannelKeyMaxLen/);
 assert.match(header,/kMeshtasticChannelKeyDefaultLen = 16/);assert.match(header,/kMeshtasticChannelKeyMaxLen = 32/);
 for(const prefix of ['mt','rt','mc.base'])for(const channel of ['primary','secondary']){
  const record={key:`${prefix}.${channel}_psk`,type:'b64'};
  if(record.key==='mc.base.secondary_psk'){for(const length of [0,16,32])assert.throws(()=>validateConfigurationField(record,new Uint8Array(length)),{code:'CONFIG_READ_ONLY'});continue;}
  for(const length of [0,16,32])validateConfigurationField(record,new Uint8Array(length));
  for(const length of [1,15,17,31,33])assert.throws(()=>validateConfigurationField(record,new Uint8Array(length)),{code:'CONFIG_CHANNEL_KEY_LENGTH'});
 }
 validateConfigurationField({key:'vendor.future.key',type:'b64'},new Uint8Array(17));
 for(const {id} of locales)assert.ok(configBinaryText(id).CONFIG_CHANNEL_KEY_LENGTH.trim());
});

test('MeshCore channel key width matches firmware and invalid imports preserve all device values',()=>{
 const length=Number(header.match(/kMeshCoreChannelKeyLen = (\d+)/)?.[1]);assert.equal(length,16);
 const record={key:'mc.channel.7.key',type:'b64'};
 validateConfigurationField(record,new Uint8Array(length));
 for(const invalid of [new Uint8Array(),new Uint8Array(15),new Uint8Array(17),new Uint8Array(32),'wrong'])assert.throws(()=>validateConfigurationField(record,invalid),{code:'CONFIG_BINARY_LENGTH'});
 const current=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Original\nmc.channel.7.key=b64:AAAAAAAAAAAAAAAAAAAAAA==\nEND\n'));
 const backup=new TmsDocument(current.serialize({kind:'backup'}));backup.set('device.node_name','Changed');backup.set(record.key,new Uint8Array(15));
 const result=prepareConfigImport(current,backup);
 assert.equal(result.errors[0].key,record.key);assert.deepEqual(result.changed,[]);assert.deepEqual(result.document.serialize(),current.serialize());
});
