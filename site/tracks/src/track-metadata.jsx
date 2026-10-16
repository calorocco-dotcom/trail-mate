import React,{useEffect,useRef,useState} from 'react';
import {Button,Card} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackMetadataText} from '../../i18n/track-metadata-labels.js';
import {saveTrackDraftMetadata} from './draft-store.js';
import {draftTrackEditorMetadata,validateTrackEditorMetadata} from './track-editor-metadata.js';

export function TrackMetadata({draft,disabled=false,onSaved,onDirty,onBusy}){
 const locale=useManagementLocale(),text=trackMetadataText(locale);
 const [value,setValue]=useState(null),[original,setOriginal]=useState(null),[input,setInput]=useState(''),[working,setWorking]=useState(false),[notice,setNotice]=useState(null);
 const epoch=useRef(0),callbacks=useRef();callbacks.current={onDirty,onBusy};
 const edited=Boolean(value&&original&&(JSON.stringify(value)!==JSON.stringify(original)||input.length));
 useEffect(()=>{callbacks.current.onDirty?.(edited);return()=>callbacks.current.onDirty?.(false);},[edited]);
 useEffect(()=>{callbacks.current.onBusy?.(working);return()=>callbacks.current.onBusy?.(false);},[working]);
 useEffect(()=>{epoch.current++;setValue(null);setOriginal(null);setInput('');setWorking(false);setNotice(null);
  try{const next=draftTrackEditorMetadata(draft);setValue(next);setOriginal(next);}catch{setNotice('invalid');}
  return()=>{epoch.current++;};
 },[draft.id,draft.version]);
 const locked=disabled||working||draft.archive||!value;
 const withInput=()=>validateTrackEditorMetadata({...value,tags:input.length?[...value.tags,input.trim()]:value.tags});
 function add(){if(locked||!input.length)return;try{setValue(withInput());setInput('');setNotice(null);}catch{setNotice('invalid');}}
 async function save(){if(locked||!edited)return;const token=epoch.current;let next;
  try{next=withInput();}catch{setNotice('invalid');return;}
  setWorking(true);setNotice(null);
  try{const saved=await saveTrackDraftMetadata(draft.id,draft.version,next);if(token!==epoch.current)return;setValue(next);setOriginal(next);setInput('');setNotice('saved');await onSaved?.(saved);}
  catch{if(token===epoch.current)setNotice('invalid');}finally{if(token===epoch.current)setWorking(false);}
 }
 return <Card className="tracks-metadata"><h2>{text.title}</h2><p>{text.intro}</p>
  {disabled&&<p>{text.unsaved}</p>}{draft.archive&&<p>{text.archive}</p>}
  <label>{text.activity}<select aria-label={text.activity} disabled={locked} value={value?.activity??0} onChange={event=>{setValue(current=>({...current,activity:Number(event.target.value)}));setNotice(null);}}>
   {text.activities.map((label,index)=><option value={index} key={index}>{label}</option>)}
   {value?.activity>8&&<option value={value.activity}>{text.unknown.replace('{value}',new Intl.NumberFormat(locale).format(value.activity))}</option>}
  </select></label>
  <label>{text.tags}<input aria-label={text.tags} value={input} disabled={locked||value.tags.length>=8} onChange={event=>{setInput(event.target.value);setNotice(null);}} onKeyDown={event=>{if(event.key==='Enter'){event.preventDefault();add();}}}/></label>
  <small>{text.tagHint}</small><Button disabled={locked||!input.length||value.tags.length>=8} onClick={add}>{text.add}</Button>
  <ul className="track-metadata-tags">{value?.tags.map((tag,index)=><li key={tag}><bdi>{tag}</bdi><Button aria-label={text.remove+' '+tag} disabled={locked} onClick={()=>{setValue(current=>({...current,tags:current.tags.filter((_,i)=>i!==index)}));setNotice(null);}}>{text.remove}</Button></li>)}</ul>
  <div className="tracks-actions"><Button disabled={locked||!edited} type="primary" onClick={save}>{text.save}</Button><Button disabled={locked||!edited} onClick={()=>{setValue(original);setInput('');setNotice(null);}}>{text.discard}</Button></div>
  {notice&&<p role={notice==='invalid'?'alert':'status'}>{text[notice]}</p>}
 </Card>;
}
export function TrackMetadataSummary({record}){
 const locale=useManagementLocale(),text=trackMetadataText(locale);if(record[0]!==2)return null;
 const metadata=record[14],activity=text.activities[metadata[0]]??text.unknown.replace('{value}',new Intl.NumberFormat(locale).format(metadata[0]));
 return <dl className="track-metadata-summary"><dt>{text.activity}</dt><dd>{activity}</dd>{metadata[1].length>0&&<><dt>{text.tags}</dt><dd><ul className="track-metadata-tags">{metadata[1].map(tag=><li key={tag}><bdi>{tag}</bdi></li>)}</ul></dd></>}</dl>;
}
