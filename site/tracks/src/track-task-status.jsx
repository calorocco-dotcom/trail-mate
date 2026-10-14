import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {OPERATION} from '../../device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {querySavedTrackDraft} from './query-saved-draft.js';
import {queryTrackHistory} from './query-history.js';
import {cancelSavedTrackJob} from './cancel-saved-job.js';
import {decodeTrackJobBinding} from './job-binding.js';
import {useTrackTaskText} from '../../i18n/management-copy.jsx';
const hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
export function TrackTaskStatus({session,operations=[],draft,historyRecord,disabled=false,acquire,release}){
 const {locale,text}=useTrackTaskText();
 const [busy,setBusy]=useState(false),[notice,setNotice]=useState(null),[job,setJob]=useState(null);
 const number=new Intl.NumberFormat(locale);
 const epoch=useRef(0),controller=useRef();
 useEffect(()=>{++epoch.current;controller.current?.abort();setBusy(false);setJob(null);setNotice(null);},[session,draft?.id,draft?.version,historyRecord?.key]);
 useEffect(()=>()=>{++epoch.current;controller.current?.abort();},[]);
 let binding=null;
 try{const raw=historyRecord?.binding??draft?.jobBinding;if(raw)binding=decodeTrackJobBinding(new Uint8Array(raw));}catch{}
 const supported=[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.JOB_GET].every(op=>operations.includes(op));
 const cancellable=operations.includes(OPERATION.JOB_CANCEL)&&job?.active&&['accepted','uploading','file-stored'].includes(job.phase);
 async function query(){
  if(busy||disabled||!session||(!draft&&!historyRecord)||!binding||!supported)return;
  const lease=acquire?.();if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;setBusy(true);setNotice({key:'verify'});
  try{
   const author=await proveDeviceAuthor(session,{signal:abort.signal});
   if(token!==epoch.current)return;
   if(abort.signal.aborted){setNotice({key:'paused'});return;}
   const options={signal:abort.signal,onProgress:value=>{if(token===epoch.current){setJob(value);setNotice({key:value.phase});}}};
   const result=historyRecord?await queryTrackHistory(session,historyRecord.key,author,options):await querySavedTrackDraft(session,draft.id,draft.version,author,options);
   if(token===epoch.current){setJob(result);setNotice({key:result.published?'versionConfirmed':['failed','cancelled'].includes(result.phase)?result.phase:!result.active?'inactive':result.phase});}
  }catch(error){if(token===epoch.current)setNotice({key:error.code==='JOB_WAIT_PAUSED'||error.name==='AbortError'?'paused':error.code==='JOB_PENDING'?'pending':'error',code:error.code});}
  finally{if(token===epoch.current)setBusy(false);release?.(lease);}
 }
 async function cancel(){
  if(busy||disabled||!session||!binding||!supported||!cancellable||!window.confirm(text.cancelConfirm))return;
  const lease=acquire?.();if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;setBusy(true);setNotice({key:'verifyCancel'});
  try{
   const author=await proveDeviceAuthor(session,{signal:abort.signal});
   if(token!==epoch.current)return;
   if(abort.signal.aborted){setNotice({key:'cancelNotSent'});return;}
   const result=await cancelSavedTrackJob(session,{historyKey:historyRecord?.key,draftId:draft?.id,draftVersion:draft?.version},author,{signal:abort.signal});
   if(token===epoch.current){setJob(result);setNotice({key:'cancelPersisted'});}
  }catch(error){if(token===epoch.current)setNotice({key:error.code==='JOB_CANCEL_PENDING'?'cancelPending':'error',code:error.code});}
  finally{if(token===epoch.current)setBusy(false);release?.(lease);}
 }
 async function copy(){try{await navigator.clipboard.writeText(hex(binding.operationId));setNotice({key:'copied'});}catch{setNotice({key:'copyFailed'});}}
 return <section className="track-preparation"><h3>{text.title}</h3>
  {!binding?<p>{draft?.jobBinding?text.invalidBinding:text.empty}</p>:<>
   <label>{text.operationId}<input readOnly dir="ltr" value={hex(binding.operationId)}/></label>
   <div className="tracks-actions"><Button disabled={busy||disabled||!session||!supported} onClick={query}>{text.query}</Button>
    <Button onClick={copy}>{text.copy}</Button><Button disabled={busy||disabled||!session||!supported||!cancellable} onClick={cancel}>{text.cancel}</Button>{busy&&<Button onClick={()=>controller.current?.abort()}>{text.pause}</Button>}</div>
   {!supported&&<p>{text.unsupported}</p>}
   {job&&<><p>{text[job.phase]??text.unknown} · <bdi>{number.format(job.offset)} / {number.format(job.fileBytes)} B</bdi></p><progress value={job.offset} max={job.fileBytes} aria-label={text.progress}/></>}
   <small>{text.help}</small>
  </>}{notice&&<p role="status">{text[notice.key]??text.unknown}{notice.key==='error'&&notice.code!=null&&<><span> · </span><bdi>{String(notice.code)}</bdi></>}</p>}
 </section>;
}
