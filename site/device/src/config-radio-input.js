import {TmsDocument} from './tms-document.js';
import {setConfigurationValue} from './config-editor-values.js';
import {MESHCORE_REGION_PRESETS,MESHCORE_PRESET_FIELDS,MESHCORE_CUSTOM_FIELDS} from './meshcore-region-presets.js';

// Only interactive edits use preset side effects. Backup import must restore
// individual stored values rather than reapply a preset in record order.
export function setConfigurationRadioInput(document,key,value){
 if(key!=='mc.region_preset'&&!MESHCORE_CUSTOM_FIELDS.includes(key)){setConfigurationValue(document,key,value);return [key];}
 const next=new TmsDocument(document.serialize()),before=new Map(document.entries().map(row=>[row.key,row]));
 setConfigurationValue(next,key,value);
 if(key==='mc.region_preset'&&value!==0){const preset=MESHCORE_REGION_PRESETS.find(row=>row.id===value);for(const [field,name] of Object.entries(MESHCORE_PRESET_FIELDS))setConfigurationValue(next,field,preset[name]);}
 else if(MESHCORE_CUSTOM_FIELDS.includes(key)&&before.has('mc.region_preset')){const original=before.get(key),different=original.type==='f32'?Math.fround(original.value)!==Math.fround(value):original.value!==value;if(different)setConfigurationValue(next,'mc.region_preset',0);}
 // Validate the whole operation before mutating the editor document.
 const changed=next.entries().filter(row=>row.raw!==before.get(row.key)?.raw);for(const row of changed)document.set(row.key,row.value);return changed.map(row=>row.key);
}
