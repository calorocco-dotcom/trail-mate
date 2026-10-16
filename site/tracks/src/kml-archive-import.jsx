import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLanguage} from '../../i18n/management-language.jsx';
import {kmlArchiveText} from '../../i18n/kml-archive-labels.js';
import {readKmlArchive,KML_ARCHIVE_LIMIT} from './kml-archive.js';
import {prepareKmlArchiveDraft} from './kml-archive-draft.js';
import {saveTrackDraft} from './draft-store.js';

export function KmlArchiveImport({disabled,onSaved}){
 const {locale}=useManagementLanguage(),text=kmlArchiveText(locale),number=new Intl.NumberFormat(locale);
 const dialog=useRef(),picker=useRef(),controller=useRef();
 const [phase,setPhase]=useState(null),[archive,setArchive]=useState(null),[main,setMain]=useState(''),[prepared,setPrepared]=useState(null),[error,setError]=useState(null);
 useEffect(()=>{if(phase&&!dialog.current.open)dialog.current.showModal();if(!phase&&dialog.current.open)dialog.current.close();},[phase]);
 useEffect(()=>()=>controller.current?.abort(),[]);
 const errorKey=error=>error.name==='QuotaExceededError'||['KML_ARCHIVE_LIMIT','ATTACHMENT_LIMIT','ATTACHMENT_SIZE','DRAFT_FULL'].includes(error.code)?'limit':error.code==='KML_ARCHIVE_SIGNED'?'signed':'failed';
 function cancel(){if(phase==='saving')return;controller.current?.abort();controller.current=null;setPhase(null);setArchive(null);setPrepared(null);setError(null);}
 function select(value,current=archive){setMain(value);setPrepared(null);setError(null);if(value)try{setPrepared(prepareKmlArchiveDraft(current,value));}catch(error){setError(errorKey(error));}}
 async function read(file){if(!file||disabled||phase==='saving')return;cancel();const active=new AbortController();controller.current=active;setPhase('reading');
  try{if(!file.size||file.size>KML_ARCHIVE_LIMIT.input)throw Object.assign(Error('Archive limit'),{code:'KML_ARCHIVE_LIMIT'});const bytes=new Uint8Array(await file.arrayBuffer());if(active.signal.aborted)return;const loaded=await readKmlArchive(bytes,{signal:active.signal});if(active.signal.aborted)return;setArchive(loaded);select(loaded.preferred??'',loaded);setPhase('review');}
  catch(error){if(!active.signal.aborted){setError(errorKey(error));setPhase('review');}}
 }
 async function commit(){if(!prepared||phase!=='review')return;setPhase('saving');setError(null);
  try{const name=Array.from(prepared.model.tree[0]?.name||prepared.filename.replace(/\.kml$/i,'')).slice(0,32).join('')||'KML';const saved=await saveTrackDraft(prepared.source,{name,filename:prepared.filename,archiveImport:{originalKml:prepared.originalKml,attachments:prepared.attachments,member:prepared.member}});setPhase(null);setArchive(null);setPrepared(null);controller.current=null;await onSaved(saved);}
  catch(error){setError(errorKey(error));setPhase('review');}
 }
 const summary=prepared&&text.summary.replace('{files}',number.format(prepared.attachments.length)).replace('{bytes}',number.format(Math.round(prepared.attachments.reduce((sum,row)=>sum+row.content.length,0)/1024))).replace('{changes}',number.format(prepared.rewritten));
 return <><Button disabled={disabled} onClick={()=>{picker.current.value='';picker.current.click();}}>{text.open}</Button><input data-kmz-picker ref={picker} hidden type="file" accept=".kmz,.zip,application/vnd.google-earth.kmz,application/zip" onChange={event=>read(event.target.files[0])}/>
  <dialog ref={dialog} className="tracks-kmz-dialog" aria-labelledby="tracks-kmz-title" onCancel={event=>{event.preventDefault();cancel();}}>
   <h2 id="tracks-kmz-title">{text.title}</h2><p>{text.help}</p>
   {phase==='reading'&&<p role="status">{text.working}</p>}
   {phase==='saving'&&<p role="status">{text.saving}</p>}
   {error&&<p role="alert">{text[error]}</p>}
   {archive&&<label>{text.main}<select data-kmz-main dir="ltr" value={main} disabled={phase==='saving'} onChange={event=>select(event.target.value)}><option value="">—</option>{archive.kmlFiles.map(name=><option key={name} value={name}>{name}</option>)}</select></label>}
   {prepared&&<><p>{summary}</p>{[['missing',prepared.missing],['external',prepared.external]].filter(([,rows])=>rows.length).map(([key,rows])=><details key={key} open><summary>{text[key]} · {number.format(rows.length)}</summary><ul>{rows.map(value=><li key={value}><bdi>{value}</bdi></li>)}</ul></details>)}</>}
   <div className="tracks-actions"><Button type="primary" disabled={!prepared||phase!=='review'} onClick={commit}>{text.commit}</Button><Button disabled={phase==='saving'} onClick={cancel}>{text.cancel}</Button></div>
  </dialog></>;
}
