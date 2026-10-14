import test from 'node:test';
import assert from 'node:assert/strict';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {locales} from '../../site/i18n/registry.js';
import {configSchemaText} from '../../site/i18n/config-schema-labels.js';
import {configNetworkFieldText} from '../../site/i18n/config-network-field-labels.js';
test('protocol/interface/delivery selectors and readable labels follow firmware enum values',()=>{
 assert.deepEqual(configurationField({key:'protocol.active',type:'u8'}).options.map(o=>o.value),[1,2,4]);
 assert.deepEqual(configurationField({key:'rt.net.interface.2.type',type:'u8'}).options.map(o=>o.value),[0,1,2]);
 assert.ok(configurationField({key:'rt.net.interface.2.target_port',type:'u16'}).label.includes('3'));
 assert.equal(configurationField({key:'mt.primary_psk',type:'b64'}).secret,true);
});
test('port/host constraints and device-owned headers reject inappropriate input',()=>{
 assert.throws(()=>validateConfigurationField({key:'rt.net.interface.0.target_port',type:'u16'},0));
 assert.throws(()=>validateConfigurationField({key:'rt.net.interface.0.target_host',type:'str'},'https://example.org/path'));
 assert.throws(()=>validateConfigurationField({key:'schema.version',type:'u16'},7));
 validateConfigurationField({key:'rt.net.interface.0.target_host',type:'str'},'example.org');
});

test('collection count is managed structurally and text lengths use UTF-8 bytes',()=>{
 assert.throws(()=>validateConfigurationField({key:'wifi.profile_count',type:'u8'},2));
 assert.throws(()=>validateConfigurationField({key:'rt.net.interface_count',type:'u8'},2));
 validateConfigurationField({key:'wifi.profile.0.ssid',type:'str'},'汉'.repeat(10));
 assert.throws(()=>validateConfigurationField({key:'wifi.profile.0.ssid',type:'str'},'汉'.repeat(11)));
 assert.throws(()=>validateConfigurationField({key:'rt.net.interface.0.id',type:'str'},'a'.repeat(24)));
 assert.throws(()=>validateConfigurationField({key:'rt.net.interface.0.passphrase',type:'str'},'a'.repeat(64)));
 assert.equal(configurationField({key:'rt.net.interface.0.ifac_size',type:'u16'}).label.includes('IFAC'),true);
});

test('numeric storage boundaries reject fractions and float32 overflow before document editing',()=>{
 for(const value of [1.5,NaN,Infinity,'12'])assert.throws(()=>validateConfigurationField({key:'rt.net.interface.0.target_port',type:'u16'},value),error=>error.code==='CONFIG_INTEGER');
 for(const value of [NaN,Infinity,-Infinity,3.5e38,'1.25'])assert.throws(()=>validateConfigurationField({key:'mt.bandwidth_khz',type:'f32'},value),error=>error.code==='CONFIG_FLOAT');
 validateConfigurationField({key:'mt.bandwidth_khz',type:'f32'},1.25);
 assert.throws(()=>validateConfigurationField({key:'rt.net.interface.0.target_port',type:'u16'},65536),error=>error.code==='CONFIG_RANGE'&&error.params.min===1&&error.params.max===65535&&error.field==='rt.net.interface.0.target_port');
 assert.throws(()=>validateConfigurationField({key:'wifi.profile.0.ssid',type:'str'},'汉'.repeat(11)),error=>error.code==='CONFIG_UTF8_LIMIT'&&error.params.max===32);
});

test('translated presentation preserves enum values and device constraints in every locale',()=>{
 const keys=['rt.net.interface.0.type','rt.net.propagation.delivery','rt.net.interface.2.ifac_size','rt.net.interface.0.target_host','gps.interval_ms'];
 for(const locale of locales){const text=configSchemaText(locale.id);for(const key of keys){const record={key,type:key.endsWith('target_host')?'str':'u16'},original=configurationField(record),translated=configurationField(record,text);assert.deepEqual(translated.range,original.range);assert.equal(translated.readOnly,original.readOnly);assert.equal(translated.secret,original.secret);assert.equal(translated.maxBytes,original.maxBytes);assert.deepEqual(translated.options?.map(option=>option.value),original.options?.map(option=>option.value));}
 assert.equal(configurationField({key:'rt.net.interface.0.type',type:'u8'},text).options[2].label,text.tcpClient);
 assert.equal(configurationField({key:'rt.net.propagation.delivery',type:'u8'},text).options[1].label,text.propagated);
 assert.equal(configurationField({key:'rt.net.interface.0.target_host',type:'str'},text).help,text.tcpHostHelp);}
});

test('network field labels use locale presentation without changing keys or unknown field names',()=>{
 for(const locale of locales){const text=configNetworkFieldText(locale.id),numbers=new Intl.NumberFormat(locale.id),presentation={...configSchemaText(locale.id),names:{net:text.network,interface:text.interface},fields:text,index:value=>numbers.format(value)};
 const field=configurationField({key:'rt.net.interface.2.target_port',type:'u16'},presentation);
 assert.equal(field.label,`Reticulum / ${text.network} / ${text.interface} / ${numbers.format(3)} · ${text.target_port}`);assert.deepEqual(field.range,[1,65535]);
 for(const last of ['future_field','constructor','__proto__'])assert.ok(configurationField({key:`rt.net.interface.2.${last}`,type:'opaque'},presentation).label.endsWith(` · ${last}`));}
});
