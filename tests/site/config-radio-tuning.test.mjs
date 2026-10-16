import test from 'node:test';import assert from 'node:assert/strict';
import {configurationRadioTuning,configurationFloatInput,formatConfigurationRadioTuning,formatConfigurationFloat} from '../../site/device/src/config-radio-tuning.js';
import {configRadioTuningText,configRadioTuningKeys} from '../../site/i18n/config-radio-tuning-labels.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configurationReview} from '../../site/device/src/config-review.js';
import {findConfigurationFields} from '../../site/device/src/config-finder.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';import {locales} from '../../site/i18n/registry.js';
const specs=[['mt.bandwidth_khz','f32',[125,250,500]],['mc.base.bandwidth_khz','f32',[125,250,500]],['mc.bandwidth_khz','f32',[125,250,500]],['rt.bandwidth_khz','f32',[125,250,500]],['mc.rx_delay_base','f32',[0,1,2,4,8,20]],['mc.airtime_factor','f32',[0,1,2,4,9]],['mc.flood_max','u8',[0,8,16,32,64]]];
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\n'+specs.map(([key,type])=>key+'='+type+':'+(type==='f32'?'1':'16')).join('\n')+'\nmt.frequency_offset_mhz=f32:0\nEND\n'));
test('all seven exact fields have translated choices with native custom-value preservation',()=>{
 for(const {id} of locales){const text=configRadioTuningText(id);assert.deepEqual(Object.keys(text),configRadioTuningKeys);if(id!=='en')assert.notEqual(text.custom,configRadioTuningText('en').custom);for(const [key,type,values] of specs){const record={key,type,value:values[0]},schema=configurationField(record,configurationPresentation(key,id));assert.deepEqual(schema.options.map(row=>row.value),values);assert.equal(schema.allowCustom,true);assert.equal(schema.customLabel,text.custom);assert.ok(schema.help);assert.equal(schema.radioTuning,true);}}
 assert.match(configRadioTuningText('zh-Hans').rxHelp,/不是秒数/);assert.match(configRadioTuningText('zh-Hans').airHelp,/不改变接收延迟/);
});
test('recognition never applies tuning semantics to unrelated fields or wrong native types',()=>{
 assert.equal(configurationRadioTuning({key:'mc.other.bandwidth_khz',type:'f32'}),undefined);assert.equal(configurationRadioTuning({key:'mc.airtime_factor',type:'u8'}),undefined);assert.equal(configurationRadioTuning({key:'mt_module.mc.rx_delay_base',type:'f32'}),undefined);
});
test('native stored bounds are not replaced by runtime clamp ranges',()=>{
 for(const [key,type] of specs){const record={key,type};if(type==='f32'){for(const value of [-1,0.1,7.8,20.5,600])validateConfigurationField(record,value);for(const value of [Infinity,NaN,3.5e38])assert.throws(()=>validateConfigurationField(record,value));}else{for(const value of [0,1,64,255])validateConfigurationField(record,value);for(const value of [-1,256,1.5])assert.throws(()=>validateConfigurationField(record,value));}}
});
test('runtime note explains clamping without changing stored value or searchable metadata',()=>{
 for(const {id} of locales)for(const [key,type,value,clamped] of [['mc.rx_delay_base','f32',30,20],['mc.airtime_factor','f32',-1,0],['mc.flood_max','u8',255,64],['mc.bandwidth_khz','f32',600,500]]){const record={key,type,value},spec=configurationRadioTuning(record,id);assert.equal(spec.runtimeNote,configRadioTuningText(id).runtime.replace('{value}',formatConfigurationRadioTuning({...record,value:clamped},id)));assert.equal(record.value,value);assert.ok(!spec.help.includes('255'));}
 assert.equal(configurationRadioTuning({key:'mt.bandwidth_khz',type:'f32',value:600}).runtimeNote,undefined);
});
test('short float32 text round-trips representative and seeded bit patterns exactly',()=>{
 const values=[-0,0,0.1,7.8,433.175,869.525,1.3333333,1.401298464e-45,3.402823466e38,-3.402823466e38],buffer=new ArrayBuffer(4),bits=new Uint32Array(buffer),float=new Float32Array(buffer);let seed=0x739ef31;
 for(let i=0;i<1000;i++){seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;bits[0]=seed>>>0;if(Number.isFinite(float[0]))values.push(float[0]);}
 for(const value of values){const native=Math.fround(value);assert.ok(Object.is(Math.fround(Number(configurationFloatInput(native))),native),String(native));}
 assert.equal(configurationFloatInput(Math.fround(0.1)),'0.1');assert.equal(configurationFloatInput(Math.fround(869.525)),'869.525');assert.equal(configurationFloatInput(-0),'-0');
});
test('review shows units and zero meanings and does not round small float edits to zero',()=>{
 const baseline=fixture(),changed=fixture();changed.set('mc.airtime_factor',2.5);changed.set('mc.rx_delay_base',0);changed.set('mc.flood_max',255);changed.set('mc.bandwidth_khz',7.8);changed.set('mt.frequency_offset_mhz',0.000001234567);
 for(const {id} of locales){
  const rows=configurationReview(changed,baseline,id);
  for(const [key] of specs.filter(([key])=>['mc.airtime_factor','mc.rx_delay_base','mc.flood_max','mc.bandwidth_khz'].includes(key)))assert.equal(rows.find(row=>row.key===key).after,formatConfigurationRadioTuning(changed.get(key),id));
  const offset=rows.find(row=>row.key==='mt.frequency_offset_mhz');
  assert.equal(offset.after,formatConfigurationFloat(changed.get(offset.key).value,id)+' MHz');assert.notEqual(offset.after,new Intl.NumberFormat(id).format(0));
 }
});
test('reading controls and searchable purposes do not mutate values or index secrets',()=>{
 const document=fixture(),before=document.serialize();const rows=document.entries().map(record=>{const schema=configurationField(record,configurationPresentation(record.key,'zh-Hans'));return {key:record.key,label:schema.label,help:schema.help??'',groupLabel:'无线'};});assert.deepEqual(document.serialize(),before);assert.ok(findConfigurationFields(rows,'空中时间').some(row=>row.key==='mc.airtime_factor'));assert.equal(findConfigurationFields(rows,'private material').length,0);
});
