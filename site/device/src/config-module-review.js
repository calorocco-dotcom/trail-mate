import {readStructuredModule} from './meshtastic-module-model.js';
import {readMeshtasticModule} from './meshtastic-module-document.js';
import {configModuleFieldLabel} from '../../i18n/config-module-labels.js';
import {configReviewText} from '../../i18n/config-review-labels.js';
import {isWebModuleField} from './config-module-web-scope.js';
import {formatModuleDuration} from './config-module-duration.js';
import {isModulePositionPrecision,formatPositionPrecision} from './config-position-precision.js';

function moduleReviewFields(model,locale,path=[],prefix='module',rows=new Map()){
 const text=configReviewText(locale),numbers=new Intl.NumberFormat(locale);
 for(const field of model.fields){
  if(!isWebModuleField([...path,field.name]))continue;
  const names=[...path,field.name],key=prefix+'.'+field.name,label=names.map((_,index)=>configModuleFieldLabel(names.slice(0,index+1),locale)).join(' · ');
  if(field.type==='MESSAGE'){
   if(field.cardinality==='REPEATED')field.value.forEach((item,index)=>moduleReviewFields(item,locale,names,key+'.'+index,rows));
   else if(field.present){rows.set(key,{label,value:true,display:text.enabled});moduleReviewFields(field.value,locale,names,key,rows);}
  }else if(field.present){
   let display;
   if(/(password|psk|key)$/.test(field.name))display=text.secret;
   else if(field.type==='BOOL')display=field.value?text.enabled:text.disabled;
   else if(field.type==='UENUM'){const option=field.options.find(item=>item.value===field.value);display=option?configModuleFieldLabel([...names,option.name],locale,'option'):numbers.format(field.value);}
   else display=isModulePositionPrecision(names,field)?formatPositionPrecision(field.value,locale,{map:true}):formatModuleDuration(names,field,locale)??(typeof field.value==='number'?numbers.format(field.value):field.value||text.empty);
   rows.set(key,{label,value:field.value,display});
  }
 }
 if(model.unknown.length)rows.set(prefix+'.unknown',{label:prefix,value:JSON.stringify(model.unknown.map(raw=>[...raw])),display:text.binary});
 return rows;
}

export function configurationModuleReview(document,baseline,locale){
 const before=moduleReviewFields(readStructuredModule(readMeshtasticModule(baseline)),locale),after=moduleReviewFields(readStructuredModule(readMeshtasticModule(document)),locale),text=configReviewText(locale);
 return [...new Set([...before.keys(),...after.keys()])].filter(key=>before.get(key)?.value!==after.get(key)?.value).map(key=>({key,
  label:(after.get(key)??before.get(key)).label,before:before.get(key)?.display??text.missing,after:after.get(key)?.display??text.missing}));
}
