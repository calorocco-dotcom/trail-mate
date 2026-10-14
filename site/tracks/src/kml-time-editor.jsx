import React,{useEffect,useMemo,useRef,useState} from 'react';
import {Button,Card} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {kmlTimeEditorText} from '../../i18n/kml-time-editor-labels.js';
import {kmlContentText} from '../../i18n/kml-content-labels.js';
import {kmlTimeEditors,activeKmlTimeValues,validateKmlTimePatch,kmlTimeChangePlan,editKmlTime} from './kml-time-edit.js';
const same=(left,right)=>JSON.stringify(activeKmlTimeValues(left))===JSON.stringify(activeKmlTimeValues(right));
const types={Document:'document',Folder:'folder',Placemark:'placemark',GroundOverlay:'ground',NetworkLink:'network',ScreenOverlay:'screen',PhotoOverlay:'photo','gx:Tour':'tour'};
export function KmlTimeEditor({model,index,disabled=false,onApply,onDirty,onSelect}){
 const locale=useManagementLocale(),text=kmlTimeEditorText(locale),pagesText=kmlContentText(locale),number=new Intl.NumberFormat(locale);
 const records=useMemo(()=>kmlTimeEditors(model),[model]),[selected,setSelected]=useState(()=>String(records.find(record=>record.featureIndex===index)?.index??0)),[search,setSearch]=useState(''),[page,setPage]=useState(0),[form,setForm]=useState(null),[base,setBase]=useState(null),[notice,setNotice]=useState(null);
 const callbacks=useRef({onDirty,onSelect});callbacks.current={onDirty,onSelect};
 const record=records[Number(selected)],pending=Boolean(form&&base&&!same(form,base));
 useEffect(()=>{const target=records.find(record=>record.featureIndex===index);if(target)setSelected(String(target.index));},[index]);
 useEffect(()=>{setSelected(previous=>String(Math.min(Number(previous)||0,Math.max(0,records.length-1))));},[model]);
 useEffect(()=>{const values=record?.local??null;setBase(values);setForm(values);setNotice(null);},[model,selected]);
 useEffect(()=>{callbacks.current.onDirty?.(pending);return()=>callbacks.current.onDirty?.(false);},[pending]);
 const matched=useMemo(()=>records.filter(record=>[...record.path,record.name,record.id??'',record.type].join(' ').toLocaleLowerCase(locale).includes(search.toLocaleLowerCase(locale))),[records,search,locale]);
 const totalPages=Math.max(1,Math.ceil(matched.length/50)),currentPage=Math.min(page,totalPages-1),shown=matched.slice(currentPage*50,currentPage*50+50);if(record&&!shown.some(row=>row.index===record.index))shown.unshift(record);
 const label=row=>text[types[row.type]]+' · '+[...row.path,row.name||row.id||text[types[row.type]]].join(' / ')+' · '+number.format(row.index+1);
 let invalid=false;try{if(form)validateKmlTimePatch(form);}catch{invalid=true;}
 const edit=(key,value)=>{setForm(previous=>({...previous,[key]:value}));setNotice(null);};
 const failure=error=>setNotice(error?.code==='KML_TIME_EDIT_REFERENCED'?'referenced':error?.code==='KML_TIME_EDIT_AMBIGUOUS'?'ambiguous':'invalid');
 function apply(){if(disabled||!pending||invalid)return;try{const plan=kmlTimeChangePlan(model,Number(selected),form);if(plan.referenced){setNotice('referenced');return;}if(form.mode==='inherit'&&!confirm(text.removeConfirm))return;const allowLoss=plan.loss.length?confirm(text.lossConfirm.replace('{items}',new Intl.ListFormat(locale).format(plan.loss))):false;if(plan.loss.length&&!allowLoss)return;const result=onApply(source=>editKmlTime(source,Number(selected),form,{allowLoss}));if(!result?.ok){failure(result?.error);return;}callbacks.current.onDirty?.(false);}catch(error){failure(error);}}
 const inherited=record?.inherited,inheritedValue=inherited?(inherited.ambiguous?text.ambiguous:inherited.mode==='stamp'?inherited.when:inherited.begin+' — '+inherited.end):'';
 return <Card className="tracks-time-editor"><h2>{text.title}</h2><p>{text.intro}</p>{disabled&&<p className="tracks-help">{text.locked}</p>}<fieldset disabled={disabled} className="tracks-edit-lock">
  <label>{text.search}<input type="search" aria-label={text.search} value={search} disabled={pending} onChange={event=>{setSearch(event.target.value);setPage(0);}}/></label>
  {records.length?<><label>{text.target}<select aria-label={text.target} value={selected} disabled={pending} onChange={event=>{const next=records[Number(event.target.value)];setSelected(event.target.value);if(next.featureIndex!==null)callbacks.current.onSelect?.(next.featureIndex);}}>{shown.map(row=><option key={row.index} value={row.index}>{label(row)}</option>)}</select></label>{totalPages>1&&<div className="tracks-actions"><Button disabled={pending||currentPage===0} onClick={()=>setPage(currentPage-1)}>{pagesText.previous}</Button><span>{pagesText.page.replace('{page}',number.format(currentPage+1)).replace('{pages}',number.format(totalPages))}</span><Button disabled={pending||currentPage===totalPages-1} onClick={()=>setPage(currentPage+1)}>{pagesText.next}</Button></div>}</>:<p>{text.empty}</p>}
  {record?.ambiguous?<p role="alert">{text.ambiguous}</p>:form&&<><p className="tracks-help">{inherited?text.inherited.replace('{owner}',inherited.owner).replace('{value}',inheritedValue):text.none}</p><label>{text.mode}<select aria-label={text.mode} value={form.mode} onChange={event=>edit('mode',event.target.value)}>{['inherit','stamp','span'].map(mode=><option key={mode} value={mode}>{text[mode]}</option>)}</select></label>
   {form.mode==='stamp'&&<label>{text.when}<input type="text" dir="ltr" aria-label={text.when} aria-invalid={invalid} value={form.when} placeholder="2026-10-14T10:00:00+08:00" onChange={event=>edit('when',event.target.value)}/></label>}
   {form.mode==='span'&&<div className="tracks-fields">{['begin','end'].map(key=><label key={key}>{text[key]}<input type="text" dir="ltr" aria-label={text[key]} aria-invalid={invalid} value={form[key]} placeholder="2026-10-14" onChange={event=>edit(key,event.target.value)}/></label>)}</div>}
   <p className="tracks-help">{text.format}</p>{(invalid||notice)&&<p role="alert">{text[notice??'invalid']}</p>}<div className="tracks-actions"><Button disabled={!pending||invalid} onClick={apply}>{text.apply}</Button><Button disabled={!pending} onClick={()=>{setForm(base);setNotice(null);callbacks.current.onDirty?.(false);}}>{text.discard}</Button></div>
  </>}
 </fieldset></Card>;
}
