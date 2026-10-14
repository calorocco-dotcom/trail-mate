import {configurationFloatInput,formatConfigurationFloat} from './config-radio-tuning.js';
import {configRadioValueText} from '../../i18n/config-radio-values-labels.js';
const unitPowers={MHz:0,kHz:3,Hz:6};
const keys={'mt.frequency_offset_mhz':'mtOffset','mt.override_frequency_mhz':'mtOverride','mc.base.frequency_offset_mhz':'legacyFrequency','mc.base.override_frequency_mhz':'legacyFrequency','mc.frequency_mhz':'mcFrequency','rt.frequency_offset_mhz':'rtOffset','rt.override_frequency_mhz':'rtOverride'};
export function configurationFrequency(record,locale='zh-Hans'){if(record.type!=='f32'||!Object.hasOwn(keys,record.key))return undefined;return {units:Object.keys(unitPowers),help:configRadioValueText(locale)[keys[record.key]]};}
export function frequencyInputDisplay(value,unit){
 if(!Number.isFinite(value)||!Number.isFinite(Math.fround(value))||!Object.hasOwn(unitPowers,unit))throw Error('Invalid frequency display');
 const text=configurationFloatInput(value),match=/^(-?)(\d+)(?:\.(\d+))?(?:e([+-]?\d+))?$/i.exec(text),[,sign,whole,fraction='',exponent='0']=match,digits=whole+fraction,index=whole.length+Number(exponent)+unitPowers[unit];
 const expanded=index<=0?'0.'+'0'.repeat(-index)+digits:index>=digits.length?digits+'0'.repeat(index-digits.length):digits.slice(0,index)+'.'+digits.slice(index);
 const [integer,decimals='']=expanded.split('.'),tail=decimals.replace(/0+$/,'');
 return sign+(integer.replace(/^0+(?=\d)/,'')||'0')+(tail?'.'+tail:'');
}
export function parseFrequencyInput(raw,unit,locale='en'){
 const reject=()=>{throw Object.assign(Error('Invalid frequency'),{code:'FREQUENCY_FORMAT',params:{frequencyUnit:unit}});};
 if(typeof raw!=='string'||raw.length>96||!Object.hasOwn(unitPowers,unit))reject();
 let text=raw.normalize('NFKC').trim().replaceAll('−','-').replace(/[٠-٩]/g,ch=>String(ch.charCodeAt(0)-0x660)).replace(/[۰-۹]/g,ch=>String(ch.charCodeAt(0)-0x6f0));
 const decimal=new Intl.NumberFormat(locale).formatToParts(1.1).find(part=>part.type==='decimal')?.value;if(decimal&&decimal!=='.')text=text.replaceAll(decimal,'.');
 const match=/^([+-]?(?:\d+(?:\.\d*)?|\.\d+))(?:e([+-]?\d+))?$/i.exec(text);if(!match)reject();
 const exponent=Number(match[2]??0);if(!Number.isSafeInteger(exponent))reject();
 const value=Number(match[1]+'e'+(exponent-unitPowers[unit]));if(!Number.isFinite(value)||!Number.isFinite(Math.fround(value)))throw Object.assign(Error('Frequency exceeds float32'),{code:'CONFIG_FLOAT',params:{frequencyUnit:unit}});
 return value;
}
export function formatConfigurationFrequency(record,locale){return configurationFrequency(record,locale)?formatConfigurationFloat(record.value,locale)+' MHz':undefined;}
