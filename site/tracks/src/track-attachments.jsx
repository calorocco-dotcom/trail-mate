import React,{useEffect,useMemo,useRef,useState} from 'react';
import {Button,Card} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackAttachmentsText} from '../../i18n/track-attachments-labels.js';
import {readTrackDraft,saveTrackDraftAttachments} from './draft-store.js';
import {validateDraftAttachmentRows,attachmentHex,attachmentDigest} from './draft-attachments.js';
import {descriptionContent} from './kml.js';
import {decodeTrackAttachmentIndex} from './attachment-index.js';
import {trackPackageText} from '../../i18n/track-package-labels.js';
import {trackSourceChangeConfirmation} from '../../i18n/track-source-labels.js';
export function TrackAttachments({draft,model,disabled=false,onSaved,onDirty,onBusy}){
 const locale=useManagementLocale(),text=trackAttachmentsText(locale),number=new Intl.NumberFormat(locale);
 const [rows,setRows]=useState([]),[working,setWorking]=useState(false),[edited,setEdited]=useState(false),[notice,setNotice]=useState(null);
 const picker=useRef(),epoch=useRef(0),dirtyCallback=useRef(onDirty);dirtyCallback.current=onDirty;
 const [unavailable,setUnavailable]=useState([]),packageText=trackPackageText(locale);
 const busyCallback=useRef(onBusy);busyCallback.current=onBusy;
 useEffect(()=>{busyCallback.current?.(working);return()=>busyCallback.current?.(false);},[working]);
 const references=useMemo(()=>Array.from(new Set([...(model?.xml?.getElementsByTagNameNS('http://www.opengis.net/kml/2.2','href')??[])].map(node=>node.textContent).concat((model?.features??[]).flatMap(feature=>descriptionContent(feature.description).images.map(image=>image.src)))).values()).filter(Boolean),[model]);
 const missing=references.filter(reference=>!rows.some(row=>row.reference===reference));
 function dirty(value){setEdited(value);dirtyCallback.current?.(value);}
 useEffect(()=>{const token=++epoch.current;setRows([]);setUnavailable([]);setWorking(true);setNotice(null);dirty(false);
  readTrackDraft(draft.id).then(snapshot=>{if(token===epoch.current){if(snapshot.version!==draft.version)throw Error('Draft changed');setRows(snapshot.attachmentRows);setUnavailable(snapshot.attachmentTemplate?decodeTrackAttachmentIndex(new Uint8Array(snapshot.attachmentTemplate))[3].filter(entry=>!snapshot.attachmentRows.some(row=>row.id===entry[0])):[]);}}).catch(()=>{if(token===epoch.current)setNotice('error');}).finally(()=>{if(token===epoch.current)setWorking(false);});
  return()=>{epoch.current++;};
 },[draft.id,draft.version]);
 const locked=disabled||working||draft.archive;
 async function add(files){
  if(locked||!files?.length)return;const selected=Array.from(files),token=epoch.current;
  setWorking(true);setNotice(null);
  try{
   const total=rows.reduce((sum,row)=>sum+row.content.length,0)+selected.reduce((sum,file)=>sum+file.size,0);
   if(rows.length+selected.length>64||draft.bytes+total>67108864||selected.some(file=>!file.size||file.size>8388608||file.type.startsWith('image/')&&file.size>4194304))throw Error('File budget');
   const additions=[];
   for(const file of selected){
    const mime=file.type||({png:'image/png',jpg:'image/jpeg',jpeg:'image/jpeg',gif:'image/gif',webp:'image/webp',bmp:'image/bmp'}[file.name.split('.').at(-1).toLowerCase()]??'application/octet-stream');
    if(mime.startsWith('image/')&&file.size>4194304)throw Error('Image budget');
    const matches=references.filter(reference=>reference===file.name||reference.endsWith('/'+file.name)),reference=matches.length===1?matches[0]:file.name;
    const content=new Uint8Array(await file.arrayBuffer());if(token!==epoch.current)return;
    additions.push({id:'a-'+crypto.randomUUID(),reference,mime,role:mime.startsWith('image/')?0:3,required:true,content,hash:attachmentHex(await attachmentDigest(content))});
   }
   if(token!==epoch.current)return;
   const next=[...rows,...additions];validateDraftAttachmentRows(next);setRows(next);dirty(true);
  }catch{if(token===epoch.current)setNotice('error');}finally{if(token===epoch.current)setWorking(false);}
 }
 function edit(index,field,value){setRows(current=>current.map((row,i)=>i===index?{...row,[field]:value}:row));dirty(true);setNotice(null);}
 async function save(){if(locked||!edited)return;if(unavailable.length&&!confirm(trackSourceChangeConfirmation(locale)))return;const token=epoch.current;setWorking(true);setNotice(null);
  try{const saved=await saveTrackDraftAttachments(draft.id,draft.version,rows);if(token!==epoch.current)return;dirty(false);setNotice('saved');await onSaved?.(saved);}
  catch{if(token===epoch.current)setNotice('error');}finally{if(token===epoch.current)setWorking(false);}
 }
 async function discard(){if(locked)return;const token=epoch.current;setWorking(true);try{const snapshot=await readTrackDraft(draft.id);if(token===epoch.current&&snapshot.version===draft.version){setRows(snapshot.attachmentRows);dirty(false);setNotice(null);}}catch{if(token===epoch.current)setNotice('error');}finally{if(token===epoch.current)setWorking(false);}}
 return <Card className="tracks-attachments"><h2>{text.title}</h2><p>{text.intro}</p><small>{text.limits}</small>
  {disabled&&<p>{text.unsaved}</p>}{draft.archive&&<p>{text.archive}</p>}
  <div className="tracks-actions"><Button disabled={locked} onClick={()=>{picker.current.value='';picker.current.click();}}>{text.add}</Button>
   <Button disabled={locked||!edited} type="primary" onClick={save}>{text.save}</Button><Button disabled={locked||!edited} onClick={discard}>{text.discard}</Button></div>
  <input ref={picker} hidden multiple type="file" onChange={event=>add(event.target.files)}/>
  {notice&&<p role={notice==='error'?'alert':'status'}>{text[notice]}</p>}{edited&&<p role="status">{text.edited}</p>}
  {!rows.length&&<p>{text.empty}</p>}
  {unavailable.length>0&&<details open><summary>{packageText.optionalMissing} · {number.format(unavailable.length)}</summary><ul>{unavailable.map(entry=><li key={entry[0]}><code dir="ltr">{entry[4]}</code> · {number.format(entry[2])} B</li>)}</ul></details>}
  <datalist id="track-attachment-references">{references.map(reference=><option key={reference} value={reference}/>)}</datalist>
  {rows.map((row,index)=><fieldset className="track-attachment-row" disabled={locked} key={index}>
   <legend><bdi>{row.reference}</bdi> · <bdi>{number.format(row.content.length)} B</bdi></legend>
   <div className="tracks-fields"><label>{text.reference}<input dir="ltr" list="track-attachment-references" value={row.reference} onChange={event=>edit(index,'reference',event.target.value)}/></label>
    <label>{text.role}<select value={row.role} onChange={event=>edit(index,'role',Number(event.target.value))}>{['photo','icon','overlay','other'].map((key,value)=><option key={key} value={value}>{text[key]}</option>)}</select></label></div>
   <label className="attachment-required"><input type="checkbox" checked={row.required} onChange={event=>edit(index,'required',event.target.checked)}/>{text.required}</label>
   <details><summary>{text.id} / {text.mime} / SHA-256</summary><div className="tracks-fields"><label>{text.id}<input dir="ltr" value={row.id} maxLength={48} onChange={event=>edit(index,'id',event.target.value)}/></label><label>{text.mime}<input dir="ltr" value={row.mime} maxLength={96} onChange={event=>edit(index,'mime',event.target.value)}/></label></div><code dir="ltr">{row.hash}</code></details>
   <Button onClick={()=>{setRows(current=>current.filter((_,i)=>i!==index));dirty(true);}}>{text.remove}</Button>
  </fieldset>)}
  {missing.length>0&&<details><summary>{text.missing} · {number.format(missing.length)}</summary><ul>{missing.map(reference=><li key={reference}><code dir="ltr">{reference}</code></li>)}</ul></details>}
 </Card>;
}
