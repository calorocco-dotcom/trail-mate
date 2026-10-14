import React,{useState} from 'react';
import {Button,Card} from 'animal-island-ui';
import {readMeshtasticModule} from './meshtastic-module-document.js';
import {moduleFieldAvailable} from './meshtastic-module-dependencies.js';
import {moduleNormalizationRule} from './meshtastic-module-normalization.js';
import {readStructuredModule,updateStructuredModuleField,editStructuredModuleRow,setStructuredModulePresence} from './meshtastic-module-model.js';
import {isWebModuleField} from './config-module-web-scope.js';
import {moduleDuration,validateModuleDuration,moduleDurationHint} from './config-module-duration.js';
import {ConfigurationDurationInput} from './config-duration-input.jsx';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {configDurationText} from '../../i18n/config-duration-labels.js';
import {configModuleDurationText} from '../../i18n/config-module-duration-labels.js';
import {isModulePositionPrecision,positionPrecisionOptions,formatPositionPrecision} from './config-position-precision.js';
import {configPrecisionText} from '../../i18n/config-precision-labels.js';
import {configMqttRuntimeText} from '../../i18n/config-mqtt-runtime-labels.js';

// Labels are supplied by the page's locale catalog; wire names remain diagnostics.
function Scalar({field,path,row,document,onChange,onValidity,disabled,label,text,errors,dependencyHelp}){
 const locale=useManagementLocale(),duration=moduleDuration(path,field),durationText=configDurationText(locale),moduleTimeText=configModuleDurationText(locale);
 const key=`module:${path.join('.')}${row?`:${row.index}:${field.name}`:''}`;
 const id=`module-${path.join('-')}${row?`-${row.index}-${field.name}`:''}`;
 const errorData=errors.get(key),raw=errorData?.raw??field.value,error=errorData?.code;
 const [revealed,setRevealed]=useState(false),[customPrecision,setCustomPrecision]=useState(false);
 const secret=/(password|psk|key)$/.test(field.name);
 const commit=value=>{
  const original=value;
  try{
   if(field.type!=='STRING'&&field.type!=='BOOL'){
    if(!String(value).trim())throw Object.assign(Error('MODULE_FIELD_INTEGER'),{code:'MODULE_FIELD_INTEGER'});
    value=Number(value);
   }
   const next=row?editStructuredModuleRow(document,path,row.index,{values:{[field.name]:value}}):updateStructuredModuleField(document,path,value);
   onChange(next);onValidity(key,true);
  }catch(failure){onValidity(key,false,{code:failure.code??failure.message,raw:original});}
 };
 const accessibility={'aria-invalid':Boolean(error),'aria-describedby':`${id}-help${error?` ${id}-error`:''}`};
 const locked=disabled||field.readOnly;
 const fieldLabel=label(row?[...path,field.name]:path,'field'),displayLabel=duration?fieldLabel.replace(/\s*[（(][^()（）]*[)）]\s*$/u,''):fieldLabel;
 let control;
 if(field.type==='BOOL')control=<label className="device-toggle"><input id={id} {...accessibility} type="checkbox" checked={Boolean(raw)} disabled={locked} onChange={event=>commit(event.target.checked)}/>{text.enabled}</label>;
 else if(field.type==='UENUM')control=<select id={id} {...accessibility} value={raw} disabled={locked} onChange={event=>commit(event.target.value)}>
  {!field.options.some(option=>option.value===Number(raw))&&<option value={raw} disabled>{text.unknownOption.replace('{value}',String(raw))}</option>}
  {field.options.map(option=><option key={option.value} value={option.value}>{label([...path,option.name],'option')}</option>)}
 </select>;
 else control=<input id={id} {...accessibility} type={secret?(revealed?'text':'password'):field.type==='STRING'?'text':'number'} value={raw} disabled={locked} min={field.min??0} max={field.max} step={1} autoComplete={secret?'new-password':undefined} spellCheck={secret?false:undefined} onChange={event=>commit(event.target.value)}/>;
 const precision=isModulePositionPrecision(path,field),precisionText=configPrecisionText(locale);
 if(precision){const options=positionPrecisionOptions(locale,{map:true}),custom=customPrecision||Boolean(error)||!options.some(option=>option.value===field.value);control=<div><select id={id} {...accessibility} value={custom?'custom':field.value} disabled={locked} onChange={event=>{setCustomPrecision(event.target.value==='custom');if(event.target.value!=='custom')commit(event.target.value);}}>{options.map(option=><option key={option.value} value={option.value}>{option.label}</option>)}<option value="custom">{precisionText.custom}</option></select>{custom&&<label>{precisionText.custom}<input type="number" aria-label={fieldLabel+' · '+precisionText.custom} {...accessibility} min={field.min??0} max={field.max} step={1} value={raw} disabled={locked} onChange={event=>commit(event.target.value)}/></label>}</div>;}
 if(duration)control=<ConfigurationDurationInput record={{key,type:'u32',value:field.value}} schema={{label:displayLabel,duration}} error={errorData} locale={locale} disabled={locked} id={id} accessibility={accessibility} validate={(_,value)=>validateModuleDuration(field,value)} onEdit={value=>{const next=updateStructuredModuleField(document,path,value);onChange(next);onValidity(key,true);}} onInvalid={(failure,raw)=>onValidity(key,false,{code:failure.code,params:failure.params??{},raw})}/>;
 const errorText=duration&&durationText[error]?durationText[error].replace(/\{(\w+)\}/g,(_,name)=>name==='unit'?durationText[duration.nativeUnit]:typeof errorData.params?.[name]==='number'?new Intl.NumberFormat(locale).format(errorData.params[name]):String(errorData.params?.[name]??'')):text.invalid+' · '+error;
 return <div className="device-field"><label htmlFor={id}>{displayLabel}</label>{control}
  {secret&&<Button aria-controls={id} aria-pressed={revealed} onClick={()=>setRevealed(value=>!value)}>{revealed?text.hide:text.show}</Button>}
  <small id={`${id}-help`}>{[dependencyHelp??(field.maxBytes!==undefined?text.byteLimit.replace('{count}',String(field.maxBytes)):field.readOnly?text.readOnly:''),text[moduleNormalizationRule(path)]].filter(Boolean).join(' ')}</small>
  {duration&&field.value===0&&<small>{moduleTimeText[moduleDurationHint(path)]}</small>}
  {precision&&<small>{precisionText.meaning} {precisionText.mapScope}</small>}
  {precision&&!error&&!positionPrecisionOptions(locale,{map:true}).some(option=>option.value===field.value)&&<small>{formatPositionPrecision(field.value,locale,{map:true})}</small>}
  {error&&<p id={`${id}-error`} role="alert" className="device-error">{errorText}</p>}
 </div>;
}

export function MeshtasticModuleEditor({document,onChange,onValidity,onNativeMqtt,disabled=false,hasInvalid=false,label,text,errors=new Map()}){
 const mqttText=configMqttRuntimeText(useManagementLocale());
 const [operationError,setOperationError]=useState();let model;
 try{model=readStructuredModule(readMeshtasticModule(document));}
 catch(error){return <Card><h2>{text.title}</h2><p role="alert" className="device-error">{text.unreadable} · <bdi>{error.code??error.message}</bdi></p></Card>;}
 const change=next=>{onChange(next);setOperationError(undefined);onValidity('module:operation',true);};
 const operate=callback=>{try{change(callback());}catch(error){setOperationError(error.code??error.message);}};
 // A failed structural action changes no bytes; allow retry after correcting fields.
 const blocked=disabled||hasInvalid;
 const render=(current,path=[])=>current.fields.map(field=>{
  const nextPath=[...path,field.name],key=nextPath.join('.');
  if(!isWebModuleField(nextPath))return null;
  if(field.type!=='MESSAGE'){
   const available=moduleFieldAvailable(model,nextPath);
   return <Scalar key={key} field={field} path={nextPath} document={document} onChange={change} onValidity={onValidity} disabled={disabled||!available} label={label} text={text} errors={errors} dependencyHelp={available?undefined:text.pwmRetained}/>;
  }
  if(field.cardinality==='REPEATED')return <section className="device-collection-row" key={key} id={`module-${nextPath.join('-')}`} tabIndex={-1}>
   <h3>{label(nextPath,'field')}</h3><p>{text.count.replace('{count}',String(field.value.length)).replace('{capacity}',String(field.maxCount))}</p>
   {field.value.map((row,index)=><section className="device-collection-row" key={index} aria-label={`${label(nextPath,'field')} ${index+1}`}>
    <h4>{label(nextPath,'field')} {index+1}</h4><div className="device-fields">{row.fields.map(item=><Scalar key={item.name} field={item} path={nextPath} row={{index}} document={document} onChange={change} onValidity={onValidity} disabled={disabled} label={label} text={text} errors={errors}/>)}</div>
    <Button disabled={blocked} onClick={()=>{if(confirm(text.confirmRemove))operate(()=>editStructuredModuleRow(document,nextPath,index,{action:'remove'}));}}>{text.remove}</Button>
   </section>)}
   <Button disabled={blocked||field.value.length>=field.maxCount} onClick={()=>operate(()=>editStructuredModuleRow(document,nextPath,field.value.length,{action:'insert'}))}>{text.addRow}</Button>
  </section>;
  return <details className="device-collection-extra" key={key}>
   <summary id={`module-${nextPath.join('-')}`}>{label(nextPath,'field')} · {field.present?text.present:text.absent}</summary>
   {key==='mqtt'&&<><p className="device-footnote">{mqttText.module}</p>{onNativeMqtt&&<Button disabled={disabled} onClick={onNativeMqtt}>{mqttText.settings}</Button>}</>}
   <p>{text[moduleNormalizationRule(nextPath)]??text.presenceHelp}</p><Button disabled={blocked} onClick={()=>{if(field.present&&!confirm(text.confirmRemove))return;operate(()=>setStructuredModulePresence(document,nextPath,!field.present));}}>{field.present?text.remove:text.addModule}</Button>
   {field.present&&<div className="device-fields">{render(field.value,nextPath)}</div>}
  </details>;
 });
 return <Card className="device-collection"><h2>{text.title}</h2><p>{text.help}</p><small>{text.durationProtocol}</small>{render(model)}{operationError&&<p role="alert" className="device-error">{text.invalid} · <bdi>{operationError}</bdi></p>}</Card>;
}
