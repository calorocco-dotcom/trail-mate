import {configRadioTuningText} from '../../i18n/config-radio-tuning-labels.js';
const bandwidthKeys=new Set(['mt.bandwidth_khz','mc.base.bandwidth_khz','mc.bandwidth_khz','rt.bandwidth_khz']);
// Find the shortest decimal that round-trips to the same native float32.
// This is presentation only: no mutation of the TMS document or stored range.
export function configurationFloatInput(value){
 const native=Math.fround(value);if(!Number.isFinite(native))return String(value);if(Object.is(native,-0))return '-0';
 for(let digits=1;digits<=9;digits++){const candidate=Number(native.toPrecision(digits));if(Object.is(Math.fround(candidate),native))return String(candidate);}
 return String(native);
}
function number(value,locale){const readable=Number(configurationFloatInput(value)),small=Math.abs(readable)>0&&Math.abs(readable)<0.0001;return new Intl.NumberFormat(locale,{maximumSignificantDigits:9,...(small||Math.abs(readable)>=1e9?{notation:'scientific'}:{})}).format(readable);}
export function formatConfigurationFloat(value,locale){return number(value,locale);}
export function configurationRadioTuning(record,locale='zh-Hans'){
 let kind;if(bandwidthKeys.has(record.key)&&record.type==='f32')kind='bandwidth';else if(record.key==='mc.rx_delay_base'&&record.type==='f32')kind='rx';else if(record.key==='mc.airtime_factor'&&record.type==='f32')kind='air';else if(record.key==='mc.flood_max'&&record.type==='u8')kind='flood';else return undefined;
 const text=configRadioTuningText(locale),format=value=>kind==='bandwidth'?number(value,locale)+' kHz':kind==='rx'?text.base.replace('{value}',number(value,locale)):kind==='air'?text.factor.replace('{value}',number(value,locale)):text.hops.replace('{value}',new Intl.NumberFormat(locale).format(value));
 const values={bandwidth:[125,250,500],rx:[0,1,2,4,8,20],air:[0,1,2,4,9],flood:[0,8,16,32,64]}[kind];
 const special=value=>kind==='rx'&&value===0?text.rxOff:kind==='rx'&&value===1?text.rxOne:kind==='air'&&value===0?text.airOff:kind==='flood'&&value===0?text.floodOff:format(value);
 const runtimeRange=kind==='rx'?[0,20]:kind==='air'?[0,9]:kind==='flood'?[0,64]:record.key==='mc.bandwidth_khz'?[7.8,500]:null;
 const value=record.value,runtimeValue=runtimeRange&&typeof value==='number'&&Number.isFinite(value)?Math.min(runtimeRange[1],Math.max(runtimeRange[0],value)):value;
 return {kind,options:values.map(value=>({value,label:special(value)})),help:text[{bandwidth:'bandwidthHelp',rx:'rxHelp',air:'airHelp',flood:'floodHelp'}[kind]],custom:text.custom,format:special,runtimeNote:runtimeRange&&runtimeValue!==value?text.runtime.replace('{value}',special(runtimeValue)):undefined};
}
export function formatConfigurationRadioTuning(record,locale){if(typeof record.value!=='number'||!Number.isFinite(record.value))return undefined;return configurationRadioTuning(record,locale)?.format(record.value);}
