import React,{useEffect,useMemo,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useKmlWorkspaceText} from '../../i18n/management-copy.jsx';
import {kmlTimeFilterText} from '../../i18n/kml-time-filter-labels.js';
import {collectKmlTimeScopes,formatKmlTimeInput,parseKmlTimeValue,kmlTimeFeatureVisible} from './kml-time.js';
export function useKmlTimeFilter(model,overlays){
 const scopes=useMemo(()=>collectKmlTimeScopes(model,overlays),[model,overlays]),[state,setState]=useState({model,range:null});
 useEffect(()=>{setState(previous=>previous.model===model?previous:{model,range:null});},[model]);
 return {scopes,range:state.model===model?state.range:null,onChange:range=>setState({model,range})};
}
export function KmlTimeFilter({scopes,range,onChange,visibleCount}){
 const {locale}=useKmlWorkspaceText(),text=kmlTimeFilterText(locale),numbers=new Intl.NumberFormat(locale);
 const [begin,setBegin]=useState(()=>formatKmlTimeInput(scopes.minimum)),[end,setEnd]=useState(()=>formatKmlTimeInput(scopes.maximum)),[touched,setTouched]=useState(false);
 useEffect(()=>{setBegin(formatKmlTimeInput(scopes.minimum));setEnd(formatKmlTimeInput(scopes.maximum));setTouched(false);},[scopes]);
 let candidate;try{const first=parseKmlTimeValue(begin),last=parseKmlTimeValue(end);if(first.start<=last.end)candidate={start:first.start,end:last.end};}catch{}
 if(!scopes.timed&&!scopes.invalid)return null;
 const visible=visibleCount??scopes.features.filter(feature=>kmlTimeFeatureVisible(feature,range)).length,extent=scopes.minimum!==null&&scopes.maximum!==null&&scopes.maximum>scopes.minimum;
 const slide=(key,raw)=>{const value=Number(raw),current=range??{start:scopes.minimum,end:scopes.maximum},next=key==='start'?{start:Math.min(value,current.end),end:current.end}:{start:current.start,end:Math.max(value,current.start)};setBegin(formatKmlTimeInput(next.start));setEnd(formatKmlTimeInput(next.end));setTouched(false);onChange(next);};
 return <section className="tracks-time-filter" aria-label={text.title}>
  <h3>{text.title}</h3><p className="tracks-help">{text.help}</p>
  <div className="tracks-time-fields"><label>{text.begin}<input type="text" dir="ltr" value={begin} aria-invalid={touched&&!candidate} onChange={event=>{setBegin(event.target.value);setTouched(true);}}/></label><label>{text.end}<input type="text" dir="ltr" value={end} aria-invalid={touched&&!candidate} onChange={event=>{setEnd(event.target.value);setTouched(true);}}/></label></div>
  {extent?<div className="tracks-time-fields"><label>{text.begin}<input type="range" aria-label={text.begin+' · UTC'} min={scopes.minimum} max={scopes.maximum} step={1} value={range?.start??scopes.minimum} aria-valuetext={formatKmlTimeInput(range?.start??scopes.minimum)} onChange={event=>slide('start',event.target.value)}/></label><label>{text.end}<input type="range" aria-label={text.end+' · UTC'} min={scopes.minimum} max={scopes.maximum} step={1} value={range?.end??scopes.maximum} aria-valuetext={formatKmlTimeInput(range?.end??scopes.maximum)} onChange={event=>slide('end',event.target.value)}/></label></div>:<p className="tracks-help">{text.empty}</p>}
  {touched&&!candidate&&<p role="alert">{text.invalid}</p>}
  <div className="tracks-actions"><Button disabled={!candidate} onClick={()=>{setTouched(false);onChange(candidate);}}>{text.apply}</Button><Button disabled={!range} onClick={()=>{setBegin(formatKmlTimeInput(scopes.minimum));setEnd(formatKmlTimeInput(scopes.maximum));setTouched(false);onChange(null);}}>{text.all}</Button></div>
  <p role="status">{text.showing.replace('{visible}',numbers.format(visible)).replace('{total}',numbers.format(scopes.features.length))}</p>
  {scopes.invalid>0&&<p className="tracks-help">{text.unparsed.replace('{count}',numbers.format(scopes.invalid))}</p>}
 </section>;
}
