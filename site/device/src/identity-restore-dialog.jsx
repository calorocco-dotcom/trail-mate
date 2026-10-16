import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {configBackupText} from '../../i18n/config-backup-labels.js';
import {identityRestoreText} from '../../i18n/identity-restore-labels.js';
import {previewEncryptedDeviceIdentity} from './identity-backup-preview.js';
import {verifyRestoredDeviceIdentity} from './identity-restore-verification.js';
import {IDENTITY_BACKUP_FILE_MAX_BYTES} from './identity-backup-file.js';
import {OPERATION} from './management-protocol.js';
export function IdentityRestore({session,operations=[],visible=true,disabled=false,acquire,release}){
 const locale=useManagementLocale(),text=identityRestoreText(locale),common=configBackupText(locale);
 const [open,setOpen]=useState(false),[file,setFile]=useState(null),[password,setPassword]=useState(''),[metadata,setMetadata]=useState(null),[consent,setConsent]=useState(false),[working,setWorking]=useState(false),[error,setError]=useState(null),[outcome,setOutcome]=useState(null);
 const dialog=useRef(),preview=useRef(),controller=useRef(),epoch=useRef(0),running=useRef(false);
 const supported=[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.IDENTITY_BACKUP_RESTORE].every(op=>operations.includes(op));
 function close(){epoch.current++;controller.current?.abort();preview.current?.close();preview.current=null;setOpen(false);setPassword('');setFile(null);setMetadata(null);setConsent(false);setError(null);}
 useEffect(()=>{close();return()=>{controller.current?.abort();preview.current?.close();};},[session]);
 useEffect(()=>{if(open&&dialog.current&&!dialog.current.open)dialog.current.showModal();},[open]);
 async function review(event){
  event.preventDefault();if(running.current||disabled||!file)return;
  const lease=session?acquire?.():null;if(session&&!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;running.current=true;setWorking(true);setError(null);
  try{if(file.size>IDENTITY_BACKUP_FILE_MAX_BYTES)throw Object.assign(Error('format'),{code:'IDENTITY_BACKUP_FORMAT_INVALID'});const bytes=new Uint8Array(await file.arrayBuffer());if(token!==epoch.current)return;const result=await previewEncryptedDeviceIdentity(bytes,password,{session,operations,signal:abort.signal});if(token!==epoch.current){result.close();return;}preview.current?.close();preview.current=result;setMetadata(result.metadata);setPassword('');setConsent(false);}
  catch(failure){if(token===epoch.current&&failure.name!=='AbortError')setError(failure.code==='IDENTITY_BACKUP_AUTH_FAILED'?'authenticationError':failure.code==='IDENTITY_BACKUP_FORMAT_INVALID'?'formatError':'error');}
  finally{running.current=false;setWorking(false);if(lease)release?.(lease);}
 }
 async function restore(){
  if(running.current||disabled||!supported||!consent||!preview.current)return;
  const lease=acquire?.();if(!lease)return;const active=preview.current;running.current=true;setWorking(true);setError(null);
  try{const result=await active.restore({consent:true});setOutcome(result);close();}
  catch(failure){if(failure.code==='IDENTITY_RESTORE_OUTCOME_UNKNOWN'){setOutcome({state:'indeterminate',expectation:failure.expectation});close();}else{setMetadata(null);setConsent(false);setError('error');}}
  finally{preview.current=null;running.current=false;setWorking(false);release?.(lease);}
 }
 async function verify(){
  if(running.current||disabled||!session||!outcome)return;const lease=acquire?.();if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;running.current=true;setWorking(true);setError(null);
  try{await verifyRestoredDeviceIdentity(session,outcome.expectation,{signal:abort.signal});if(token===epoch.current)setOutcome(value=>({...value,state:'verified'}));}
  catch(failure){if(token===epoch.current&&failure.name!=='AbortError')setError('notConfirmed');}
  finally{running.current=false;setWorking(false);release?.(lease);}
 }
 return <section className="device-identity-restore" hidden={!visible&&!outcome}>
  <Button disabled={disabled||working||Boolean(outcome)} onClick={()=>setOpen(true)}>{text.title}</Button>
  {!supported&&<p>{text.unsupported}</p>}
  {outcome&&<div role="status"><p>{outcome.state==='verified'?text.verified:outcome.state==='reboot-required'?text.reboot:text.unknown}</p><code dir="ltr">{outcome.expectation.identityHash}</code><div className="device-backup-actions"><Button disabled={disabled||working||!session||outcome.state==='verified'} onClick={verify}>{text.verify}</Button><Button disabled={working} onClick={()=>{setOutcome(null);setError(null);}}>{text.clear}</Button></div></div>}
  {error&&!open&&<p role="alert">{text[error]??common[error]}</p>}
  {open&&<dialog ref={dialog} className="device-backup-dialog" aria-label={text.title} onCancel={event=>{event.preventDefault();close();}}><form onSubmit={review}>
   <h2>{text.title}</h2><p>{text.help}</p>
   {!metadata?<><div className="device-field"><label>{text.file}<input type="file" accept=".tmid,application/octet-stream" disabled={working} onChange={event=>{setFile(event.target.files?.[0]??null);setPassword('');setError(null);}}/></label></div><div className="device-field"><label>{common.password}<input type="password" autoComplete="current-password" value={password} disabled={working} onChange={event=>setPassword(event.target.value)}/></label></div></>:
    <><dl><dt>{text.source}</dt><dd><code dir="ltr">{metadata.identityHash}</code></dd><dt>{text.current}</dt><dd><code dir="ltr">{metadata.targetIdentityHash??text.noDevice}</code></dd><dt>{common.generation}</dt><dd>{metadata.targetGeneration??'—'} → {metadata.targetGeneration&&metadata.targetGeneration<0xffffffff?metadata.targetGeneration+1:'—'}</dd></dl><p>{metadata.changesIdentity===null?text.noDevice:metadata.changesIdentity?text.different:text.same}</p><label><input type="checkbox" checked={consent} disabled={working} onChange={event=>setConsent(event.target.checked)}/>{text.consent}</label></>}
   {working&&<p role="status">{common.working}</p>}{error&&<p role="alert">{common[error]??text[error]}</p>}
   <div className="device-backup-actions"><Button onClick={close}>{common.cancel}</Button>{metadata?<Button type="primary" disabled={working||!consent||!supported||!session||!metadata.restoreSupported||metadata.targetGeneration===0xffffffff} onClick={restore}>{text.restore}</Button>:<Button type="primary" htmlType="submit" disabled={working||!file||!password}>{text.preview}</Button>}</div>
  </form></dialog>}
 </section>;
}
