import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {configBackupText} from '../../i18n/config-backup-labels.js';
import {identityBackupText} from '../../i18n/identity-backup-labels.js';
import {OPERATION} from './management-protocol.js';
import {exportEncryptedDeviceIdentity} from './identity-backup-export.js';
export function IdentityBackupExport({session,operations=[],disabled=false,acquire,release,onFile}){
 const locale=useManagementLocale(),text=identityBackupText(locale),common=configBackupText(locale);
 const [open,setOpen]=useState(false),[password,setPassword]=useState(''),[confirmation,setConfirmation]=useState(''),[consent,setConsent]=useState(false),[working,setWorking]=useState(false),[error,setError]=useState(null);
 const dialog=useRef(),controller=useRef(),epoch=useRef(0),running=useRef(false);
 const supported=[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.IDENTITY_BACKUP_EXPORT].every(value=>operations.includes(value));
 function close(){epoch.current++;controller.current?.abort();setOpen(false);setPassword('');setConfirmation('');setConsent(false);setError(null);}
 useEffect(()=>{close();return()=>{epoch.current++;controller.current?.abort();};},[session]);
 useEffect(()=>{if(open&&dialog.current&&!dialog.current.open)dialog.current.showModal();},[open]);
 async function submit(event){
  event.preventDefault();if(running.current||disabled||!supported||!session)return;
  if(password!==confirmation){setError('passwordMismatch');return;}
  const lease=acquire?.();if(!lease)return;const token=epoch.current,abort=new AbortController();controller.current=abort;running.current=true;setWorking(true);setError(null);
  try{const result=await exportEncryptedDeviceIdentity(session,password,{consent,signal:abort.signal});if(token!==epoch.current||abort.signal.aborted)return;onFile?.(result.bytes,result.author);close();}
  catch(failure){if(token===epoch.current&&failure.name!=='AbortError')setError(failure.code==='IDENTITY_BACKUP_PASSWORD_INVALID'?'passwordError':failure.code==='IDENTITY_BACKUP_CRYPTO_UNAVAILABLE'?'cryptoError':'error');}
  finally{running.current=false;setWorking(false);release?.(lease);}
 }
 return <section className="device-identity-backup"><Button disabled={disabled||!session||!supported} onClick={()=>setOpen(true)}>{text.title}</Button>{!supported&&<p>{text.unsupported}</p>}
  {open&&<dialog ref={dialog} className="device-backup-dialog" aria-label={text.title} onCancel={event=>{event.preventDefault();close();}}>
   <form onSubmit={submit}><h2>{text.title}</h2><p>{text.help}</p><p>{common.exportHelp}</p>
    <div className="device-field"><label>{common.password}<input type="password" autoComplete="new-password" value={password} disabled={working} onChange={event=>setPassword(event.target.value)}/></label><small>{common.passwordHelp}</small></div>
    <div className="device-field"><label>{common.confirmation}<input type="password" autoComplete="new-password" value={confirmation} disabled={working} onChange={event=>setConfirmation(event.target.value)}/></label></div>
    <label><input type="checkbox" checked={consent} disabled={working} onChange={event=>setConsent(event.target.checked)}/>{text.consent}</label>
    {error&&<p role="alert">{common[error]}</p>}{working&&<p role="status">{common.working}</p>}
    <div className="device-backup-actions"><Button onClick={close}>{common.cancel}</Button><Button type="primary" htmlType="submit" disabled={working||!consent||!password||!confirmation}>{common.encrypt}</Button></div>
   </form>
  </dialog>}
 </section>;
}
