import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackSharedText} from '../../i18n/track-shared-labels.js';
import {verifyTrackManifest} from './manifest.js';
import {trackId,trackShareLink,copyTrackReference} from './shared-id.js';

export function TrackShareControls({record,disabled=false}){
 const text=trackSharedText(useManagementLocale()),[reference,setReference]=useState(null),[notice,setNotice]=useState(null),idInput=useRef(),linkInput=useRef();
 useEffect(()=>{
  let current=true;setReference(null);setNotice(null);
  if(record?.stage==='confirmed')verifyTrackManifest([record.plan.manifest,record.signature],{expectedAuthor:record.authorKey}).then(value=>{
   if(current)setReference({id:trackId(value.objectId),link:trackShareLink(new URL('../',location.href).href,value.objectId,{revision:value.revisionHash})});
  }).catch(()=>{});
  return()=>{current=false;};
 },[record]);
 async function copy(kind){
  const result=await copyTrackReference(reference[kind]);setNotice(result.copied?'copied':'selectCopy');
  if(!result.copied){const control=kind==='id'?idInput.current:linkInput.current;control?.focus();control?.select();}
 }
 if(!reference)return null;
 return <section className="track-publication-share" aria-label={text.copyLink}>
  <label>{text.id}<input ref={idInput} dir="ltr" readOnly value={reference.id} onFocus={event=>event.target.select()}/></label>
  <label>{text.copyLink}<input ref={linkInput} dir="ltr" readOnly value={reference.link} onFocus={event=>event.target.select()}/></label>
  <div className="tracks-actions"><Button disabled={disabled} onClick={()=>copy('id')}>{text.copyId}</Button><Button disabled={disabled} onClick={()=>copy('link')}>{text.copyLink}</Button></div>
  {notice&&<p role="status">{text[notice]}</p>}
 </section>;
}
