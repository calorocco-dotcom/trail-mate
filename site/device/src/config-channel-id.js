const keys=new Set(['mt.primary_channel_id','mt.secondary_channel_id','mc.base.primary_channel_id','mc.base.secondary_channel_id','rt.primary_channel_id','rt.secondary_channel_id']);
export function isConfigurationChannelId(record){return record.type==='u32'&&keys.has(record.key);}
const reject=()=>{throw Object.assign(Error('Invalid channel identifier'),{code:'CONFIG_CHANNEL_ID'});};
export function parseConfigurationChannelId(raw,base){
 if(typeof raw!=='string'||raw.length>64||!['decimal','hex'].includes(base))reject();
 const text=raw.normalize('NFKC').trim().replace(/[٠-٩]/g,ch=>String(ch.charCodeAt(0)-0x660)).replace(/[۰-۹]/g,ch=>String(ch.charCodeAt(0)-0x6f0));
 if(base==='decimal'?!/^\d+$/.test(text):! /^(?:0x)?[0-9a-f]{1,8}$/i.test(text))reject();
 const value=BigInt(base==='hex'?'0x'+text.replace(/^0x/i,''):text);if(value>4294967295n)reject();return Number(value);
}
export function configurationChannelIdInput(value,base){if(!Number.isInteger(value)||value<0||value>4294967295||!['decimal','hex'].includes(base))reject();return base==='hex'?'0x'+value.toString(16).padStart(8,'0').toUpperCase():String(value);}
export function formatConfigurationChannelId(value,locale){return configurationChannelIdInput(value,'hex')+' · '+new Intl.NumberFormat(locale).format(value);}
