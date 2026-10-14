import test from 'node:test';import assert from 'node:assert/strict';
import {CAPABILITY,decodeHello} from '../../site/device/src/hostlink.js';
import {configurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configPrecisionText} from '../../site/i18n/config-precision-labels.js';
import {configPositionRuntimeText} from '../../site/i18n/config-position-runtime-labels.js';
import {locales} from '../../site/i18n/registry.js';
test('existing HELLO layout carries the position capability independently of management or labels',()=>{
 const payload=new Uint8Array([1,0,0,2,0,4,0,0,6,...new TextEncoder().encode('T-Deck'),4,...new TextEncoder().encode('test')]);
 assert.equal(decodeHello(payload).capabilities,CAPABILITY.MANAGEMENT);
 new DataView(payload.buffer).setUint32(4,CAPABILITY.MANAGEMENT|CAPABILITY.CHANNEL_POSITION_PRECISION,true);
 assert.equal(decodeHello(payload).capabilities&CAPABILITY.CHANNEL_POSITION_PRECISION,2048);
 assert.equal(payload.length,20);
});
test('eleven-language help changes only for an advertised position capability, without changing values or constraints',()=>{
 for(const {id} of locales)for(const channel of ['primary','secondary']){
  const record={key:`channel.${channel}_position_precision`,type:'u32',value:4294967295},presentation=configurationPresentation(record.key,id),text=configPrecisionText(id);
  const legacy=configurationField(record,presentation),unknown=configurationField(record,presentation,null,{firmwareCapabilities:1<<12}),supported=configurationField(record,presentation,null,{firmwareCapabilities:CAPABILITY.CHANNEL_POSITION_PRECISION});
  assert.equal(legacy.help,text.meaning+' '+text.channelScope);assert.equal(unknown.help,legacy.help);
  assert.equal(supported.help,text.meaning+' '+configPositionRuntimeText(id));assert.notEqual(supported.help,legacy.help);
  assert.deepEqual(supported.options,legacy.options);assert.deepEqual(supported.range,[0,4294967295]);assert.equal(record.value,4294967295);
  const moduleFlag={key:`channel.${channel}_has_module`,type:'bool',value:false};configurationField(moduleFlag,presentation,null,{firmwareCapabilities:CAPABILITY.CHANNEL_POSITION_PRECISION});assert.equal(moduleFlag.value,false);
 }
 assert.throws(()=>configPositionRuntimeText('unknown'));
});
