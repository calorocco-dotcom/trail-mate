import {configRadioValueText} from '../../i18n/config-radio-values-labels.js';
import {configRadioTuningText} from '../../i18n/config-radio-tuning-labels.js';
export function configurationRadioValue(record,locale='zh-Hans'){
 const text=configRadioValueText(locale),numbers=new Intl.NumberFormat(locale);
 if(record.key==='mt.channel_num'&&record.type==='u16')return {kind:'channel',options:[0,1,2,3,4,5,6,7,8].map(value=>({value,label:value===0?text.autoChannel:text.manualChannel.replace('{value}',numbers.format(value))})),customLabel:text.custom,help:text.mtChannel};
 const kind={'mt.tx_power':'mtPower','mc.base.tx_power':'mcPower','rt.tx_power':'rtPower'}[record.key];if(record.type!=='i8'||!kind)return undefined;
 const values=kind==='mcPower'?[1,5,10,12,17,20,22,30]:kind==='rtPower'?[0,5,10,12,17,20,22]:[0,5,10,12,17,20,22,30],format=value=>value===0&&kind!=='mcPower'?text[kind==='mtPower'?'mtAutoPower':'rtAutoPower']:numbers.format(value)+' dBm';
 const value=record.value,runtimeValue=typeof value!=='number'?value:kind==='mcPower'?Math.min(30,Math.max(1,value)):kind==='rtPower'?value===0?17:Math.min(22,Math.max(-9,value)):Math.max(-9,value);
 return {kind:'power',options:values.map(value=>({value,label:format(value)})),customLabel:text.custom,help:text[kind],format,runtimeNote:typeof value==='number'&&value!==runtimeValue?configRadioTuningText(locale).runtime.replace('{value}',numbers.format(runtimeValue)+' dBm'):undefined};
}
export function formatConfigurationRadioValue(record,locale){const spec=configurationRadioValue(record,locale);if(spec?.kind==='power')return spec.format(record.value);if(spec?.kind==='channel')return record.value===0?configRadioValueText(locale).autoChannel:configRadioValueText(locale).manualChannel.replace('{value}',new Intl.NumberFormat(locale).format(record.value));}
