import {validateConfigurationField} from './config-schema.js';
import {equal} from '../../geocaching/src/protocol.js';

// Decoder::finish() derives this compatibility buffer from the active channel.
// Keep the existing output length (0/16/32) and zero padding; it is not a second
// independent channel key. Unknown/partial schemas remain untouched.
export function synchronizeMeshCoreChannelMirror(document){
 const records=new Map(document.entries().map(record=>[record.key,record])),slot=records.get('mc.active_slot'),mirror=records.get('mc.base.secondary_psk');
 if(slot?.type!=='u8'||!Number.isInteger(slot.value)||slot.value<0||slot.value>7||mirror?.type!=='b64'||!(mirror.value instanceof Uint8Array)||![0,16,32].includes(mirror.value.length))return;
 const channel=records.get(`mc.channel.${slot.value}.key`);if(channel?.type!=='b64'||!(channel.value instanceof Uint8Array)||channel.value.length!==16)return;
 const next=new Uint8Array(mirror.value.length);next.set(channel.value.subarray(0,Math.min(next.length,16)));
 if(!equal(mirror.value,next))document.set(mirror.key,next);
}

export function setConfigurationValue(document,key,value){
 validateConfigurationField(document.get(key),value);
 document.set(key,value);
 if(key==='mc.active_slot'||/^mc\.channel\.\d+\.key$/.test(key))synchronizeMeshCoreChannelMirror(document);
}
