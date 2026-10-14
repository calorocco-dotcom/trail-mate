import {configDurationText} from '../../i18n/config-duration-labels.js';
const specs={
 'gps.init_probe_ms':['u32','milliseconds'], 'gps.interval_ms':['u32','milliseconds'], 'gps.motion_idle_ms':['u32','milliseconds'],
 'rt.net.propagation.sync_interval_s':['u32','seconds'],
 'aprs.tx_min_interval_s':['u16','seconds'], 'aprs.dedupe_window_s':['u16','seconds'], 'aprs.position_interval_s':['u16','seconds'],
};
export function configurationDuration(record){
 const spec=specs[record.key];if(!spec||record.type!==spec[0])return undefined;
 return {nativeUnit:spec[1],units:spec[1]==='milliseconds'?[{key:'milliseconds',factor:1},{key:'seconds',factor:1000},{key:'minutes',factor:60000},{key:'hours',factor:3600000}]:[{key:'seconds',factor:1},{key:'minutes',factor:60},{key:'hours',factor:3600}]};
}
export function defaultDurationUnit(value,spec){return [...spec.units].reverse().find(unit=>value>0&&Number.isSafeInteger(value)&&value%unit.factor===0)??spec.units[0];}
const factors=new Set([1,60,1000,3600,60000,3600000]);
export function durationDisplay(value,factor){
 if(!Number.isSafeInteger(value)||value<0||!factors.has(factor))throw Error('Invalid duration display');
 const n=BigInt(value),d=BigInt(factor);let remainder=n%d,digits='';
 while(remainder&&digits.length<12){remainder*=10n;digits+=String(remainder/d);remainder%=d;}
 return {text:String(n/d)+(digits?'.'+digits:''),approximate:remainder!==0n};
}
export function parseDurationInput(raw,factor,locale='en'){
 const reject=code=>{throw Object.assign(Error(code),{code,params:{durationFactor:factor}});};
 if(!factors.has(factor)||typeof raw!=='string'||raw.length>64)reject('DURATION_FORMAT');
 // Arabic input remains valid when the locale's ICU default uses Latin digits.
 let text=raw.normalize('NFKC').trim().replace(/[٠-٩]/g,ch=>String(ch.charCodeAt(0)-0x660)).replace(/[۰-۹]/g,ch=>String(ch.charCodeAt(0)-0x6f0)).replaceAll('٫','.');
 const decimal=new Intl.NumberFormat(locale).formatToParts(1.1).find(part=>part.type==='decimal')?.value;
 if(decimal&&decimal!=='.')text=text.replaceAll(decimal,'.');
 if(!/^(?:\d+(?:\.\d*)?|\.\d+)$/.test(text))reject('DURATION_FORMAT');
 const [whole,fraction='']=text.split('.'),denominator=10n**BigInt(fraction.length),numerator=BigInt((whole||'0')+fraction)*BigInt(factor);
 if(numerator%denominator!==0n)reject('DURATION_PRECISION');
 const value=numerator/denominator;if(value>BigInt(Number.MAX_SAFE_INTEGER))reject('DURATION_OVERFLOW');return Number(value);
}
export function formatConfigurationDuration(record,locale){
 const spec=configurationDuration(record);if(!spec||!Number.isSafeInteger(record.value)||record.value<0)return undefined;
 const unit=defaultDurationUnit(record.value,spec),text=configDurationText(locale);
 return new Intl.NumberFormat(locale).format(record.value/unit.factor)+' '+text[unit.key];
}
