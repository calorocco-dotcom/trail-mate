import test from 'node:test';import assert from 'node:assert/strict';
import {configurationDuration,defaultDurationUnit,durationDisplay,parseDurationInput,formatConfigurationDuration} from '../../site/device/src/config-duration.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configurationReview} from '../../site/device/src/config-review.js';import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configDurationText} from '../../site/i18n/config-duration-labels.js';import {locales} from '../../site/i18n/registry.js';
test('duration parsing uses exact decimal arithmetic and rejects precision loss, groups and huge values',()=>{
 for(const [raw,factor,value] of [['1.5',60000,90000],['0.001',1000,1],['1.',1000,1000],['.25',60000,15000],['0',3600000,0],['4294967.295',1000,4294967295],['9007199254740.991',1000,Number.MAX_SAFE_INTEGER]])assert.equal(parseDurationInput(raw,factor),value);
 for(const [raw,factor] of [['0.0001',1000],['1.5',1],['0.000000001',3600000]])assert.throws(()=>parseDurationInput(raw,factor),{code:'DURATION_PRECISION'});
 for(const raw of ['','-1','1e3','1,234','NaN','Infinity','1..2','1 000','a','1'.repeat(65)])assert.throws(()=>parseDurationInput(raw,1000,'en'),{code:'DURATION_FORMAT'});
 assert.throws(()=>parseDurationInput('9007199254740.992',1000),{code:'DURATION_OVERFLOW'});assert.throws(()=>parseDurationInput('1',2),{code:'DURATION_FORMAT'});
});
test('localized decimal separators and Arabic digits remain exact',()=>{
 for(const {id} of locales){const separator=new Intl.NumberFormat(id).formatToParts(1.1).find(part=>part.type==='decimal').value;assert.equal(parseDurationInput('1'+separator+'5',60000,id),90000);assert.equal(parseDurationInput('1.5',60000,id),90000);const text=configDurationText(id);assert.ok(Object.values(text).every(value=>value.trim()));}
 for(const locale of ['ar','ar-u-nu-latn']){assert.equal(parseDurationInput('١٫٥',60000,locale),90000);assert.equal(parseDurationInput('۱٫۵',60000,locale),90000);assert.throws(()=>parseDurationInput('١٬٥',60000,locale),{code:'DURATION_FORMAT'});}assert.equal(parseDurationInput('１.５',60000),90000);
});
test('display distinguishes approximate fractions without changing any native value',()=>{
 assert.deepEqual(durationDisplay(60000,60000),{text:'1',approximate:false});assert.deepEqual(durationDisplay(1,1000),{text:'0.001',approximate:false});assert.equal(durationDisplay(1,60000).approximate,true);
 for(const factor of [1,60,1000,3600,60000,3600000])for(const value of [0,1,3,9,900,1234,60000,65535,4294967295]){const display=durationDisplay(value,factor);if(!display.approximate)assert.equal(parseDurationInput(display.text,factor),value);}
 const spec=configurationDuration({key:'gps.interval_ms',type:'u32'});assert.equal(defaultDurationUnit(60000,spec).key,'minutes');assert.equal(defaultDurationUnit(900,spec).key,'milliseconds');assert.equal(defaultDurationUnit(0,spec).key,'milliseconds');assert.equal(defaultDurationUnit(3600000,spec).key,'hours');
});
test('only verified duration fields use units; special behavior values remain options',()=>{
 for(const key of ['ui.screen.timeout_ms','map.track_interval','gps.init_baud','unknown.interval_ms'])assert.equal(configurationDuration({key,type:'u32'}),undefined);
 assert.equal(configurationDuration({key:'gps.interval_ms',type:'str'}),undefined);
 for(const {id} of locales){const schema=configurationField({key:'gps.interval_ms',type:'u32'},configurationPresentation('gps.interval_ms',id));assert.ok(schema.duration);assert.ok(!/[（(][^()（）]*[)）]$/u.test(schema.label));}
 assert.deepEqual(configurationField({key:'rt.net.propagation.sync_interval_s',type:'u32'}).range,[60,86400]);for(const value of [60,900,86400])validateConfigurationField({key:'rt.net.propagation.sync_interval_s',type:'u32'},value);for(const value of [0,59,86401])assert.throws(()=>validateConfigurationField({key:'rt.net.propagation.sync_interval_s',type:'u32'},value),{code:'CONFIG_RANGE'});
});
test('review attaches native duration meaning in every locale while preserving snapshots',()=>{
 const baseline=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ngps.interval_ms=u32:60000\nEND\n')),after=new TmsDocument(baseline.serialize());after.set('gps.interval_ms',90000);
 for(const {id} of locales){const row=configurationReview(after,baseline,id)[0],text=configDurationText(id);assert.equal(row.before,new Intl.NumberFormat(id).format(1)+' '+text.minutes);assert.equal(row.after,new Intl.NumberFormat(id).format(90)+' '+text.seconds);assert.equal(formatConfigurationDuration({key:'gps.interval_ms',type:'u32',value:1234},id),new Intl.NumberFormat(id).format(1234)+' '+text.milliseconds);}
 assert.equal(baseline.get('gps.interval_ms').value,60000);
});
