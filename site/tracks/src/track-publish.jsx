import React,{useEffect,useMemo,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {OPERATION} from '../../device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {useTrackTaskText} from '../../i18n/management-copy.jsx';
import {trackPublishText} from '../../i18n/track-publish-labels.js';
import {commitSavedTrackDraft} from './commit-saved-draft.js';
import {querySavedTrackDraft} from './query-saved-draft.js';
export function TrackPublish({session,operations=[],draft,prepared,directory,disabled=false,acquire,release,onSubmitted}){
 const {locale,text:task}=useTrackTaskText(),text=useMemo(()=>trackPublishText(locale),[locale]),number=new Intl.NumberFormat(locale);
 const [busy,setBusy]=useState(false),[attempted,setAttempted]=useState(false),[target,setTarget]=useState(null),[job,setJob]=useState(null),[notice,setNotice]=useState(null);
 const epoch=useRef(0),controller=useRef(),running=useRef(false);
 useEffect(()=>{epoch.current++;controller.current?.abort();running.current=false;setBusy(false);setAttempted(false);setJob(null);setNotice(null);return()=>{epoch.current++;controller.current?.abort();};},[session,draft?.id,draft?.version,prepared]);
 const querySupported=[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.JOB_GET].every(op=>operations.includes(op)),supported=querySupported&&operations.includes(OPERATION.TRACK_COMMIT);
 async function run(resume=false){
  if(running.current||disabled||!session||!draft||!prepared||!querySupported||!resume&&(!supported||!directory?.capabilitiesVerified||attempted))return;
  const lease=acquire?.();if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;running.current=true;setBusy(true);setNotice('verify');
  const current=()=>{if(token!==epoch.current||abort.signal.aborted)throw abort.signal.reason??new DOMException('Aborted','AbortError');};
  try{
   const author=await proveDeviceAuthor(session,{signal:abort.signal});current();
   if(!resume){
    // Once submission starts, subsequent actions query the durable original
    // binding. A pause or lost reply cannot authorize a new operation.
    await commitSavedTrackDraft(session,draft.id,draft.version,prepared,directory,author,{signal:abort.signal,onProgress:()=>{if(token===epoch.current){setAttempted(true);setTarget({name:directory.name});onSubmitted?.();}}});current();
   }
   const result=await querySavedTrackDraft(session,draft.id,draft.version,author,{signal:abort.signal,onProgress:value=>{if(token===epoch.current){setJob(value);setNotice(null);}}});current();setJob(result);setNotice(null);
  }catch(error){if(token===epoch.current)setNotice(['COMMIT_PENDING','JOB_PENDING','JOB_WAIT_PAUSED'].includes(error.code)||error.name==='AbortError'?'pending':'error');}
  finally{if(token===epoch.current){running.current=false;setBusy(false);}release?.(lease);}
 }
 return <section className="track-publish"><h4>{text.title}</h4>
  {attempted?<p><bdi>{target?.name}</bdi></p>:directory?.capabilitiesVerified?<p><bdi>{directory.name}</bdi></p>:<p>{text.directory}</p>}
  {!supported&&<p>{text.unsupported}</p>}
  <div className="tracks-actions"><Button disabled={disabled||busy||!supported||!directory?.capabilitiesVerified||attempted} onClick={()=>run()}>{text.publish}</Button>
   {attempted&&<Button disabled={disabled||busy||!querySupported} onClick={()=>run(true)}>{text.resume}</Button>}{busy&&<Button onClick={()=>controller.current?.abort()}>{text.pause}</Button>}</div>
  {job&&<><p role="status">{job.published?task.versionConfirmed:!job.active&&!['failed','cancelled'].includes(job.phase)?task.inactive:task[job.phase]} · <bdi>{number.format(job.offset)} / {number.format(job.fileBytes)} B</bdi></p><progress value={job.offset} max={job.fileBytes} aria-label={task.progress}/></>}
  {notice&&<p role="status">{notice==='error'?task.error:text[notice]}</p>}<small>{text.help}</small>
 </section>;
}
