import test from 'node:test';import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configRadioPolicyText} from '../../site/i18n/config-radio-policy-labels.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {locales} from '../../site/i18n/registry.js';
const header=readFileSync(new URL('../../modules/core_chat/include/chat/domain/chat_types.h',import.meta.url),'utf8');
const choices={
 'rt.interface_policy':['ReticulumInterfacePolicy',{All:0,LoRaOnly:1,WifiGatewayOnly:2},'reticulumPolicies'],
 'mc.send_profile':['MeshCorePayloadSendProfile',{V1Only:0,AutoPreferV2:1,V2Only:2},'meshCoreSendProfiles'],
 'mc.forward_profile':['MeshCoreForwardProfile',{Any:0,MultibyteOnly:1},'meshCoreForwardProfiles'],
};
test('policy selectors match authoritative firmware enum values in eleven languages',()=>{
 for(const [key,[name,expected,property]] of Object.entries(choices)){
  const body=header.match(new RegExp(`enum class ${name} : uint8_t\\s*\\{([^}]+)\\}`))?.[1];assert.ok(body);
  const actual=Object.fromEntries([...body.matchAll(/(\w+)\s*=\s*(\d+)/g)].map(match=>[match[1],Number(match[2])]));assert.deepEqual(actual,expected);
  const values=Object.values(expected),record={key,type:'u8'};
  for(const value of values)validateConfigurationField(record,value);
  for(const value of [-1,values.length,255])assert.throws(()=>validateConfigurationField(record,value));
  for(const {id} of locales){const schema=configurationField(record,configurationPresentation(key,id));assert.deepEqual(schema.options.map(option=>option.value),values);assert.deepEqual(schema.options.map(option=>option.label),configRadioPolicyText(id)[property]);}
 }
});
test('invalid policy import preserves the entire original configuration',()=>{
 const current=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Original\nrt.interface_policy=u8:0\nmc.send_profile=u8:1\nmc.forward_profile=u8:1\nEND\n'));
 for(const key of Object.keys(choices)){
  const backup=new TmsDocument(current.serialize({kind:'backup'}));backup.set(key,255);backup.set('device.node_name','Changed');
  const result=prepareConfigImport(current,backup);assert.equal(result.errors[0].key,key);assert.deepEqual(result.changed,[]);assert.deepEqual(result.document.serialize(),current.serialize());
 }
});
