import {configNumericPurposeText} from '../../i18n/config-numeric-purpose-labels.js';
import {configRoutingValueText} from '../../i18n/config-routing-values-labels.js';import {isConfigurationChannelId,formatConfigurationChannelId} from './config-channel-id.js';
// Exact TMSET keys and types only: never assign semantics to a future/wrong type.
export function configurationNumericPurpose(record,locale='zh-Hans'){
 const text=configNumericPurposeText(locale),numbers=new Intl.NumberFormat(locale),format=(key,value)=>text[key].replace('{value}',()=>numbers.format(value));
 const routing=configRoutingValueText(locale),read=(key,value)=>routing[key].replace('{value}',()=>numbers.format(value));
 if(isConfigurationChannelId(record))return {kind:'channelId',help:routing.idHelp,format:value=>formatConfigurationChannelId(value,locale)};
 if(record.type==='u8'&&['policy.hop_limit','policy.max_retries','policy.max_channels','mt.hop_limit','mc.base.hop_limit','rt.hop_limit'].includes(record.key)){
  const policy=record.key.startsWith('policy.'),kind=record.key==='policy.max_retries'?'retries':record.key==='policy.max_channels'?'channels':'hops',values=kind==='retries'?[0,1,2,3,4,5]:kind==='channels'?[1,2,3]:record.key==='mt.hop_limit'?[0,1,2,3,4,5,6,7]:[0,1,2,3];
  return {kind,help:policy?routing.policyHelp:record.key==='mt.hop_limit'?routing.mtHelp:routing.compatHop,options:values.map(value=>({value,label:read(kind,value)})),custom:routing.custom,format:value=>read(kind,value),note:record.key==='mt.hop_limit'&&record.value>7?routing.wire.replace('{value}',()=>numbers.format(record.value&7)):undefined};
 }
 if(record.type==='u16'&&['mc.base.channel_num','rt.channel_num'].includes(record.key))return {kind:'compatNumber',help:routing.compatNumber,format:value=>read('number',value)};
 if(record.type==='i32'&&record.key==='ui.timezone.offset_minutes')return {kind:'offset',format:value=>routing.offset.replace('{offset}',()=>`UTC${value<0?'-':'+'}${String(Math.floor(Math.abs(value)/60)).padStart(2,'0')}:${String(Math.abs(value)%60).padStart(2,'0')}`).replace('{value}',()=>read('minutes',value))};
 if(record.type==='u16'&&(/^(mt|mc)\.mqtt\.port$/.test(record.key)||record.key==='rt.wifi_port'||/^rt\.net\.interface\.[0-5]\.(target_port|discovery_port|data_port)$/.test(record.key))){const specialized=/\.mqtt\.port$/.test(record.key)?text.mqttHelp:record.key==='rt.wifi_port'?text.gatewayHelp:undefined;return {kind:'port',help:specialized?specialized+' '+text.portHelp:undefined,format:value=>text.port.replace('{value}',()=>new Intl.NumberFormat(locale,{useGrouping:false}).format(value))};}
 if(record.type==='u32'&&/^power\.gauge\.(design|full)_mah$/.test(record.key))return {kind:'capacity',label:record.key.endsWith('design_mah')?text.design:text.full,help:text.capacityHelp,format:value=>format('capacity',value)};
 if(record.type==='u8'&&record.key==='rt.net.propagation.max_messages_per_sync')return {kind:'sync',help:text.syncHelp,options:[1,8,16,32,64].map(value=>({value,label:format('messages',value)})),custom:text.custom,format:value=>format('messages',value)};
 if(record.type==='u8'&&record.key==='aprs.igate_ssid')return {kind:'ssid',help:text.ssidHelp,options:Array.from({length:16},(_,value)=>({value,label:value===0?text.noSuffix:format('suffix',value)})),custom:text.custom,format:value=>value===0?text.noSuffix:format('suffix',value),note:record.value>15?text.ssidUnsupported:undefined};
}
export function formatConfigurationNumericPurpose(record,locale){return typeof record.value==='number'?configurationNumericPurpose(record,locale)?.format(record.value):undefined;}
