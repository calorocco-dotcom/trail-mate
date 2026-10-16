import test from 'node:test';import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configTimezoneText} from '../../site/i18n/config-timezone-labels.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {locales} from '../../site/i18n/registry.js';
test('timezone choices match device IDs, standard offsets and DST profiles',()=>{
 const source=readFileSync(new URL('../../modules/core_sys/src/platform/ui/timezone_profile.cpp',import.meta.url),'utf8');
 const ids=Object.fromEntries([...source.matchAll(/constexpr int (k\w+) = (\d+);/g)].map(match=>[match[1],Number(match[2])]));
 const profiles=[...source.matchAll(/\{(k\w+), "[^"]+", "[^"]+", (-?\d+), (-?\d+), DstRuleKind::(\w+)\}/g)].map(match=>({id:ids[match[1]],offset:Number(match[2]),dst:match[4]!=='None'}));
 assert.equal(profiles.length,24);
 const header=readFileSync(new URL('../../modules/core_sys/include/platform/ui/timezone_profile.h',import.meta.url),'utf8');assert.match(header,/kFixedTimezoneProfileId = -1/);
 for(const {id} of locales){
  const text=configTimezoneText(id),schema=configurationField({key:'ui.timezone.profile',type:'i32'},configurationPresentation('ui.timezone.profile',id));
  assert.deepEqual(schema.options,text.timezoneProfiles);assert.deepEqual(schema.options.map(row=>row.value),[-1,...profiles.map(row=>row.id)]);assert.equal(schema.help,text.timezoneProfileHelp);
  assert.equal(configurationField({key:'ui.timezone.offset_minutes',type:'i32'},configurationPresentation('ui.timezone.offset_minutes',id)).help,text.timezoneOffsetHelp);
  for(const profile of profiles){const label=schema.options[profile.id+1].label,offset=profile.offset,expected=offset===0?'UTC':`UTC${offset<0?'−':'+'}${Math.floor(Math.abs(offset)/60)}:${String(Math.abs(offset)%60).padStart(2,'0')}`;assert.ok(label.includes(expected));assert.equal(label.split(' · ').length,profile.dst?3:2);}
 }
 for(const value of [-1,...profiles.map(row=>row.id)])validateConfigurationField({key:'ui.timezone.profile',type:'i32'},value);
 for(const value of [-2,24,2147483647])assert.throws(()=>validateConfigurationField({key:'ui.timezone.profile',type:'i32'},value));
});
test('invalid timezone import rolls back all edits while retaining opaque timezone bytes',()=>{
 const current=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Original\nui.timezone.profile=i32:1\nui.timezone.offset_minutes=i32:480\nui.timezone.tzdef=b64:AAECAw==\nEND\n'));
 const backup=new TmsDocument(current.serialize({kind:'backup'}));backup.set('ui.timezone.profile',99);backup.set('device.node_name','Changed');
 const result=prepareConfigImport(current,backup);assert.equal(result.errors[0].key,'ui.timezone.profile');assert.deepEqual(result.changed,[]);assert.deepEqual(result.document.serialize(),current.serialize());
 backup.set('ui.timezone.profile',-1);backup.set('ui.timezone.offset_minutes',330);const valid=prepareConfigImport(current,backup);assert.deepEqual(valid.errors,[]);assert.equal(valid.document.get('ui.timezone.profile').value,-1);assert.equal(valid.document.get('ui.timezone.offset_minutes').value,330);assert.deepEqual(valid.document.get('ui.timezone.tzdef').value,current.get('ui.timezone.tzdef').value);
});
