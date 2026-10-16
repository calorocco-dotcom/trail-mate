import React,{useEffect,useState} from 'react';
import {Button} from 'animal-island-ui';
import {simplifyKmlGeometry} from './kml-simplify.js';
import {useKmlWorkspaceText} from '../../i18n/management-copy.jsx';
import {kmlSimplifyText} from '../../i18n/kml-simplify-labels.js';
export function KmlSimplify({source,index,editor,disabled,onPreview,onApply}){
 const {locale,text:workspace}=useKmlWorkspaceText(),text=kmlSimplifyText(locale),number=new Intl.NumberFormat(locale,{maximumFractionDigits:2}),[tolerance,setTolerance]=useState('5'),[prepared,setPrepared]=useState(null),[error,setError]=useState(null);
 useEffect(()=>{setPrepared(null);setError(null);onPreview(null);return()=>onPreview(null);},[source,index,editor,disabled,onPreview]);
 if(!['LineString','gx:Track'].includes(editor?.type))return null;
 return <fieldset className="tracks-simplify" disabled={disabled}><legend>{text.title}</legend><p className="tracks-help">{text.help}</p>
  <label>{text.tolerance}<input type="number" min="0.1" max="10000" step="0.1" value={tolerance} onChange={event=>{setTolerance(event.target.value);setPrepared(null);setError(null);onPreview(null);}}/></label>
  <div className="tracks-actions"><Button disabled={disabled} onClick={()=>{try{const candidate=simplifyKmlGeometry(source,index,editor.path,Number(tolerance));setPrepared(candidate);setError(null);onPreview(candidate);document.getElementById('tracks-workspace-map')?.scrollIntoView({block:'center'});}catch(error){setPrepared(null);onPreview(null);setError(error.code??'SIMPLIFY_UNSUPPORTED');}}}>{text.preview}</Button>
   <Button disabled={disabled||!prepared||prepared.after===prepared.before} onClick={()=>onApply(Number(tolerance))}>{text.apply}</Button><Button disabled={!prepared} onClick={()=>{setPrepared(null);onPreview(null);}}>{text.cancel}</Button></div>
  {error&&<p role="alert">{text[error]??text.SIMPLIFY_UNSUPPORTED}</p>}{prepared&&<><p role="status">{text.pending}</p><p>{text.count}: <bdi dir="ltr">{number.format(prepared.before)} → {number.format(prepared.after)}</bdi></p>
   {['distanceM','ascentM','durationS'].map((key,index)=>{const units=['km','m','s'],format=value=>value===null?workspace.unknown:number.format(value/(index===0?1000:1))+' '+units[index];return <p key={key}>{workspace[['distance','ascent','duration'][index]].replace('{value}',format(prepared.statistics.before[key])+' → '+format(prepared.statistics.after[key]))}</p>;})}
  </>}
 </fieldset>;
}
