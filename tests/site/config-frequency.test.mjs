import test from 'node:test';
import assert from 'node:assert/strict';
import {configurationFrequency,frequencyInputDisplay,parseFrequencyInput,formatConfigurationFrequency} from '../../site/device/src/config-frequency.js';
import {configurationRadioValue,formatConfigurationRadioValue} from '../../site/device/src/config-radio-values.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configRadioValueText,configRadioValueKeys} from '../../site/i18n/config-radio-values-labels.js';
import {locales} from '../../site/i18n/registry.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationReview} from '../../site/device/src/config-review.js';
const frequencyKeys=['mt.frequency_offset_mhz','mt.override_frequency_mhz','mc.base.frequency_offset_mhz','mc.base.override_frequency_mhz','mc.frequency_mhz','rt.frequency_offset_mhz','rt.override_frequency_mhz'];
test('frequency unit conversion preserves every tested float32 bit pattern including extrema and negative zero',()=>{
 const values=[0,-0,0.0125,-0.0125,495.2,869.525,Math.fround(3.4028234663852886e38),Math.fround(-3.4028234663852886e38),Math.fround(1.401298464324817e-45)],view=new DataView(new ArrayBuffer(4));let seed=0x42f4510d;
 for(let i=0;i<1000;i++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;view.setUint32(0,seed);const value=view.getFloat32(0);if(Number.isFinite(value))values.push(value);}
 for(const value of values)for(const unit of ['MHz','kHz','Hz']){const raw=frequencyInputDisplay(value,unit),parsed=parseFrequencyInput(raw,unit);assert.ok(Object.is(Math.fround(parsed),Math.fround(value)),`${unit}: ${raw}`);assert.ok(raw.length<=96);}
 assert.equal(frequencyInputDisplay(0.0125,'kHz'),'12.5');assert.equal(frequencyInputDisplay(869.525,'Hz'),'869525000');assert.equal(parseFrequencyInput('-12.5','kHz'),-0.0125);assert.equal(parseFrequencyInput('12.5e3','Hz'),0.0125);
});
test('localized decimal frequencies accept native digits and reject malformed, grouped and overflowing inputs',()=>{
 for(const {id} of locales){const raw=new Intl.NumberFormat(id,{useGrouping:false}).format(12.5);assert.equal(parseFrequencyInput(raw,'kHz',id),0.0125);assert.equal(parseFrequencyInput('-12.5','kHz',id),-0.0125);}
 assert.equal(parseFrequencyInput('−１２.５','kHz'),-0.0125);assert.equal(parseFrequencyInput('۱۲.۵','kHz'),0.0125);
 for(const raw of ['', ' ', 'NaN','Infinity','0x10','1/2','1 234','1,234','1e','--1','1e99999999999999999999','1'.repeat(97)])assert.throws(()=>parseFrequencyInput(raw,'MHz'),{code:'FREQUENCY_FORMAT'});
 assert.throws(()=>parseFrequencyInput('3.5e38','MHz'),{code:'CONFIG_FLOAT'});assert.throws(()=>parseFrequencyInput('1','GHz'),{code:'FREQUENCY_FORMAT'});assert.equal(parseFrequencyInput('3.5e38','Hz'),3.5e32);
});
test('exact field/type recognition supplies translated units and protocol-specific meaning without changing stored ranges',()=>{
 for(const {id} of locales){const text=configRadioValueText(id);assert.deepEqual(Object.keys(text),configRadioValueKeys);for(const key of frequencyKeys){const record={key,type:'f32',value:869.525},schema=configurationField(record,configurationPresentation(key,id));assert.deepEqual(schema.frequency.units,['MHz','kHz','Hz']);assert.ok(schema.help.length>0);assert.equal(schema.range,undefined);assert.ok(!schema.label.endsWith('(MHz)'));assert.ok(formatConfigurationFrequency(record,id).endsWith(' MHz'));}
  for(const key of ['mt.tx_power','mc.base.tx_power','rt.tx_power']){const schema=configurationField({key,type:'i8',value:0},configurationPresentation(key,id));assert.deepEqual(schema.range,[-128,127]);assert.equal(schema.allowCustom,true);assert.ok(schema.options.length>1);for(const value of [-128,-9,0,1,22,30,127])assert.doesNotThrow(()=>validateConfigurationField({key,type:'i8'},value));}
  const channel=configurationField({key:'mt.channel_num',type:'u16',value:65535},configurationPresentation('mt.channel_num',id));assert.deepEqual(channel.range,[0,65535]);assert.equal(channel.allowCustom,true);assert.equal(channel.options[0].label,text.autoChannel);
 }
 for(const key of frequencyKeys)assert.equal(configurationFrequency({key,type:'u32'}),undefined);for(const key of ['vendor.frequency_mhz','gps.frequency_offset_mhz'])assert.equal(configurationFrequency({key,type:'f32'}),undefined);
 for(const key of ['mc.tx_power','vendor.tx_power','mc.base.channel_num','rt.channel_num'])assert.equal(configurationRadioValue({key,type:'i8'}),undefined);
});
test('power zero meanings, custom signed values and runtime notes remain distinct without rewriting stored values',()=>{
 const mt={key:'mt.tx_power',type:'i8',value:0},mc={key:'mc.base.tx_power',type:'i8',value:0},rt={key:'rt.tx_power',type:'i8',value:0};
 assert.match(formatConfigurationRadioValue(mt,'zh-Hans'),/区域/);assert.equal(formatConfigurationRadioValue(mc,'zh-Hans'),'0 dBm');assert.match(formatConfigurationRadioValue(rt,'zh-Hans'),/17 dBm/);assert.match(configurationRadioValue(mc).runtimeNote,/1 dBm/);assert.match(configurationRadioValue(rt).runtimeNote,/17 dBm/);
 for(const [record,value,runtime] of [[mc,-128,1],[mc,127,30],[rt,-128,-9],[rt,127,22],[mt,-128,-9]]){const original={...record,value},spec=configurationRadioValue(original);assert.ok(spec.runtimeNote.includes(String(runtime)));assert.equal(original.value,value);}
 const source=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmc.frequency_mhz=f32:869.525\nmc.base.tx_power=i8:20\nmt.channel_num=u16:0\nEND\n')),changed=new TmsDocument(source.serialize());changed.set('mc.frequency_mhz',495.2);changed.set('mc.base.tx_power',8);changed.set('mt.channel_num',65535);
 for(const {id} of locales){const rows=configurationReview(changed,source,id);assert.ok(rows.find(row=>row.key==='mc.frequency_mhz').after.endsWith(' MHz'));assert.ok(rows.find(row=>row.key==='mc.base.tx_power').after.endsWith(' dBm'));assert.equal(rows.find(row=>row.key==='mt.channel_num').after,formatConfigurationRadioValue(changed.get('mt.channel_num'),id));}
});
