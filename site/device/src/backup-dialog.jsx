import React,{useEffect,useId,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {encryptConfigBackup,decryptConfigBackupDetails} from './config-backup.js';
import {useConfigBackupText} from '../../i18n/management-copy.jsx';
import {ManagementLanguage} from '../../i18n/management-language.jsx';
const errorKey=error=>({BACKUP_AUTH_FAILED:'authenticationError',BACKUP_FORMAT_INVALID:'formatError',BACKUP_DOCUMENT_INVALID:'formatError',BACKUP_PARAMETERS_UNSUPPORTED:'parametersError',BACKUP_PASSWORD_INVALID:'passwordError',BACKUP_CRYPTO_UNAVAILABLE:'cryptoError'}[error.code]??'error');
export function BackupDialog({operation,currentAuthor,onResult,onClose,onBusy}){
 const {locale,text}=useConfigBackupText();
 const dialog=useRef(),input=useRef(),flight=useRef(false),title=useId(),passwordId=useId(),confirmId=useId();
 const [password,setPassword]=useState(''),[confirmation,setConfirmation]=useState(''),[working,setWorking]=useState(false),[error,setError]=useState(null);
 const [decoded,setDecoded]=useState();
 const exporting=operation.mode==='export';
 useEffect(()=>{const modal=dialog.current,origin=window.document.activeElement;modal.showModal();input.current.focus();return()=>{if(modal.open)modal.close();if(origin?.isConnected)origin.focus();};},[]);
 function close(){if(flight.current)return;setPassword('');setConfirmation('');onClose();}
 async function run(){
  if(flight.current)return;
  if(exporting&&password!==confirmation){setError('passwordMismatch');return;}
  flight.current=true;setWorking(true);setError(null);onBusy(true);
  try{
   const result=exporting?await encryptConfigBackup(operation.document,password,{source:operation.source}):await decryptConfigBackupDetails(operation.bytes,password);
   setPassword('');setConfirmation('');
   if(exporting){onResult(result);onClose();}else setDecoded(result);
  }catch(error){setError(errorKey(error));}
  finally{flight.current=false;setWorking(false);onBusy(false);}
 }
 return <dialog className="device-backup-dialog" ref={dialog} aria-labelledby={title} onCancel={event=>{event.preventDefault();close();}}>
  <h2 id={title}>{exporting?text.exportTitle:text.importTitle}</h2>
  <ManagementLanguage page="device"/>
  <p>{exporting?text.exportHelp:text.importHelp}</p>
  <p className="device-footnote">{text.scope}</p>
  {decoded?<><h3>{text.sourceTitle}</h3>{decoded.source?<dl>
   <dt>{text.model}</dt><dd><bdi>{decoded.source.model??text.missing}</bdi></dd><dt>{text.firmware}</dt><dd><bdi>{decoded.source.firmware??text.missing}</bdi></dd>
   <dt>{text.time}</dt><dd><time dateTime={decoded.source.createdAt} title={decoded.source.createdAt}>{new Intl.DateTimeFormat(locale,{dateStyle:'medium',timeStyle:'medium'}).format(new Date(decoded.source.createdAt))}</time></dd><dt>{text.fingerprint}</dt><dd className="device-backup-fingerprint">{decoded.source.authorFingerprint?<bdi dir="ltr">{decoded.source.authorFingerprint}</bdi>:text.unverifiedExport}</dd>
   <dt>{text.generation}</dt><dd>{decoded.source.authorGeneration==null?text.missing:new Intl.NumberFormat(locale).format(decoded.source.authorGeneration)}</dd></dl>:<p>{text.legacy}</p>}
   {decoded.source?.authorFingerprint&&currentAuthor?.verified&&(decoded.source.authorFingerprint!==currentAuthor.fingerprint||decoded.source.authorGeneration!==currentAuthor.generation)&&<p role="status">{text.mismatch}</p>}
   {decoded.source?.authorFingerprint&&!currentAuthor?.verified&&<p role="status">{text.unverifiedCurrent}</p>}
   <p className="device-footnote">{text.sourceHelp}</p>
   {error&&<p role="alert" className="device-error">{text[error]}</p>}<div className="device-backup-actions"><Button onClick={close}>{text.cancel}</Button><Button type="primary" onClick={()=>{try{onResult(decoded.document);onClose();}catch(error){setError(errorKey(error));}}}>{text.continue}</Button></div>
  </>:<form onSubmit={event=>{event.preventDefault();run();}}>
   <div className="device-field"><label htmlFor={passwordId}>{text.password}</label><input ref={input} id={passwordId} type="password" autoComplete={exporting?'new-password':'off'} value={password} disabled={working} onChange={event=>setPassword(event.target.value)}/>{exporting&&<small>{text.passwordHelp}</small>}</div>
   {exporting&&<div className="device-field"><label htmlFor={confirmId}>{text.confirmation}</label><input id={confirmId} type="password" autoComplete="new-password" value={confirmation} disabled={working} onChange={event=>setConfirmation(event.target.value)}/></div>}
   {error&&<p role="alert" className="device-error">{text[error]}</p>}
   {working&&<p role="status">{text.working}</p>}
   <div className="device-backup-actions"><Button disabled={working} onClick={close}>{text.cancel}</Button><Button type="primary" htmlType="submit" disabled={working||!password||(exporting&&(Array.from(password).length<12||password!==confirmation))}>{exporting?text.encrypt:text.decrypt}</Button></div>
  </form>}
 </dialog>;
}
