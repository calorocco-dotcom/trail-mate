import {configurationField} from './config-schema.js';
import {configurationPresentation} from './config-field-presentation.js';
import {configSectionText} from '../../i18n/config-section-labels.js';
export function configurationSections(records,locale){
 const sections=new Map(),gps=configSectionText(locale);
 for(const record of records){
  let key=record.key.split('.').slice(0,-1).join('.'),label;
  if(record.key.startsWith('gps.')){
   const kind=record.key.startsWith('gps.init_')?'startup':record.key.startsWith('gps.motion_')?'motion':record.key.startsWith('gps.external_nmea_')?'output':'position';
   key='gps.'+kind;label=gps[kind];
  }else{
   const full=configurationField(record,configurationPresentation(record.key,locale)).label,index=full.lastIndexOf(' · ');
   label=index>=0?full.slice(0,index):full;
  }
  if(!sections.has(key))sections.set(key,{key,label,records:[]});
  sections.get(key).records.push(record);
 }
 return [...sections.values()];
}
