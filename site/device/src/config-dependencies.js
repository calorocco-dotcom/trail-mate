// Match the current Meshtastic radio selection. Disabling a control never
// deletes or rewrites the retained custom values in the TMS document.
export function configurationFieldAvailable(document,key){
 let preset;try{preset=document?.get('mt.use_preset');}catch{return true;}
 if(preset?.type!=='bool'||typeof preset.value!=='boolean')return true;
 if(['mt.bandwidth_khz','mt.spread_factor','mt.coding_rate'].includes(key))return !preset.value;
 if(key==='mt.modem_preset')return preset.value;
 return true;
}
