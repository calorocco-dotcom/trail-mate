import {readMeshtasticModule} from './meshtastic-module-document.js';
import {readStructuredModule} from './meshtastic-module-model.js';
import {isWebModuleField} from './config-module-web-scope.js';
import {moduleDuration} from './config-module-duration.js';
import {isModulePositionPrecision} from './config-position-precision.js';

// Keys mirror editor validation keys; targets mirror its real DOM controls.
// A missing optional module points to its presence control, never adds itself.
export function configurationModuleSearchRows(document,baseline,{label,text,group,groupLabel,index=value=>String(value)}={}){
 let model,original;
 try{model=readStructuredModule(readMeshtasticModule(document));}catch{return [];}
 try{original=readStructuredModule(readMeshtasticModule(baseline));}catch{original=readStructuredModule(new Uint8Array());}
 const values=new Map(),rows=[];
 const visit=(current,path=[],unavailable,collect=false,rowContext)=>{
  for(const field of current.fields){
   if(!isWebModuleField([...path,field.name]))continue;
   const fieldPath=rowContext?path:[...path,field.name],key=rowContext?`module:${path.join('.')}:${rowContext.index}:${field.name}`:`module:${fieldPath.join('.')}`,
    ownTarget=rowContext?`module-${path.join('-')}-${rowContext.index}-${field.name}`:`module-${fieldPath.join('-')}`,
    targetId=unavailable??ownTarget;
   const scalar=field.type!=='MESSAGE',token=scalar?{present:field.present,value:field.value}:{present:field.present,count:field.cardinality==='REPEATED'?field.value.length:undefined};
   if(!collect)values.set(key,token);
   else{
    const before=values.get(key),changed=!before||before.present!==token.present||before.count!==token.count||scalar&&before.value!==token.value;
    const parts=(rowContext?[...path,field.name]:fieldPath).map((part,position)=>label((rowContext?[...path,field.name]:fieldPath).slice(0,position+1),'field'));
    if(rowContext)parts.splice(parts.length-1,0,index(rowContext.index+1));
    const duration=moduleDuration(rowContext?[...path,field.name]:fieldPath,field),unitHelp=duration?[text[duration.nativeUnit],text.durationProtocol].filter(Boolean).join(' · '):'';
    rows.push({key,label:parts.join(' · '),help:[unavailable?`${text.absent} · ${text.presenceHelp}`:text.help,unitHelp,isModulePositionPrecision(fieldPath,field)?text.precisionHelp:''].filter(Boolean).join(' '),group,groupLabel,targetId,changed});
   }
   if(scalar)continue;
   if(field.cardinality==='REPEATED'){
    if(field.value.length)field.value.forEach((row,position)=>visit(row,fieldPath,unavailable,collect,{index:position}));
    else visit(readStructuredModule(new Uint8Array(),field.message),fieldPath,targetId,collect,{index:0});
   }else visit(field.value??readStructuredModule(new Uint8Array(),field.message),fieldPath,unavailable??(field.present?undefined:ownTarget),collect);
  }
 };
 visit(original);visit(model,[],undefined,true);
 return rows;
}
