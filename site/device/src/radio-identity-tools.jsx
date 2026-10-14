import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {configBackupText} from '../../i18n/config-backup-labels.js';
import {identityRestoreText} from '../../i18n/identity-restore-labels.js';
import {radioIdentityText} from '../../i18n/radio-identity-labels.js';
import {proveDeviceAuthor} from './author-identity.js';
import {OPERATION} from './management-protocol.js';
import {RADIO_IDENTITY_FILE_MAX_BYTES,readDeviceRadioIdentity,exportEncryptedRadioIdentity,decryptRadioIdentityBackup,clearRadioIdentityBackup,restoreRadioIdentityBackup,verifyRestoredRadioIdentity} from './radio-identity.js';
const hex=value=>value?Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join(''):null;
export function RadioIdentityTools({session,operations=[],visible=true,disabled=false,acquire,release,onFile}){
 const locale=useManagementLocale(),text=radioIdentityText(locale),common=configBackupText(locale),restoration=identityRestoreText(locale);
 const [kind,setKind]=useState(1),[info,setInfo]=useState(null),[mode,setMode]=useState(null),[metadata,setMetadata]=useState(null),[file,setFile]=useState(null),[password,setPassword]=useState(''),[confirmation,setConfirmation]=useState(''),[consent,setConsent]=useState(false),[working,setWorking]=useState(false),[error,setError]=useState(null),[outcome,setOutcome]=useState(null);
 const decoded=useRef(),target=useRef(),current=useRef(),controller=useRef(),epoch=useRef(0),running=useRef(false),alive=useRef(true),dialog=useRef();
 const supported=[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.RADIO_IDENTITY_INFO,OPERATION.RADIO_IDENTITY_BACKUP_EXPORT,OPERATION.RADIO_IDENTITY_BACKUP_RESTORE].every(op=>operations.includes(op));
 function close(){epoch.current++;controller.current?.abort();clearRadioIdentityBackup(decoded.current);decoded.current=null;target.current=null;current.current=null;setMode(null);setMetadata(null);setPassword('');setConfirmation('');setFile(null);setConsent(false);setError(null);}
 useEffect(()=>{close();setInfo(null);},[session]);
 useEffect(()=>{alive.current=true;return()=>{alive.current=false;epoch.current++;controller.current?.abort();clearRadioIdentityBackup(decoded.current);};},[]);
 useEffect(()=>{if(mode&&dialog.current&&!dialog.current.open)dialog.current.showModal();},[mode]);
 async function run(action,device=true){
  if(running.current||disabled||device&&(!session||!supported))return;
  const lease=device?acquire?.():null;if(device&&!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;running.current=true;setWorking(true);setError(null);
  try{await action(abort.signal,()=>alive.current&&token===epoch.current&&!abort.signal.aborted);}
  catch(failure){if(alive.current&&failure.code==='RADIO_IDENTITY_OUTCOME_UNKNOWN'){setOutcome({state:'indeterminate',expectation:failure.expectation});close();}
   else if(alive.current&&token===epoch.current&&failure.name!=='AbortError')setError(failure.code==='IDENTITY_BACKUP_AUTH_FAILED'?'authenticationError':failure.code==='IDENTITY_BACKUP_PASSWORD_INVALID'?'passwordError':failure.code==='IDENTITY_BACKUP_CRYPTO_UNAVAILABLE'?'cryptoError':failure.code==='RADIO_IDENTITY_FILE_INVALID'?'formatError':'error');}
  finally{running.current=false;if(alive.current)setWorking(false);if(lease)release?.(lease);}
 }
 function read(){run(async(signal,live)=>{const author=await proveDeviceAuthor(session,{signal}),radio=await readDeviceRadioIdentity(session,kind,author,{operations,signal});if(live())setInfo({present:radio.present,generation:radio.generation,publicKey:hex(radio.publicKey)});});}
 function submit(event){event.preventDefault();if(mode==='export'){
   if(password!==confirmation){setError('passwordMismatch');return;}
   run(async(signal,live)=>{const result=await exportEncryptedRadioIdentity(session,kind,password,{operations,consent,signal});if(live()){onFile?.(result.bytes,`trail-mate-${kind===1?'meshcore':'meshtastic'}-identity.tmrf`);close();}});
  }else run(async(signal,live)=>{
   if(!file||file.size>RADIO_IDENTITY_FILE_MAX_BYTES)throw Object.assign(Error('format'),{code:'RADIO_IDENTITY_FILE_INVALID'});
   const bytes=new Uint8Array(await file.arrayBuffer());const value=await decryptRadioIdentityBackup(bytes,password);if(!live()){clearRadioIdentityBackup(value);return;}
   clearRadioIdentityBackup(decoded.current);decoded.current=value;setKind(value.identity.kind);
   if(session&&supported){target.current=await proveDeviceAuthor(session,{signal});current.current=await readDeviceRadioIdentity(session,value.identity.kind,target.current,{operations,signal});}
   if(!live()){clearRadioIdentityBackup(value);return;}
   setMetadata({kind:value.identity.kind,publicKey:hex(value.identity.publicKey),sourceAuthor:hex(value.identity.sourceAuthor),sourceGeneration:value.identity.radioGeneration,
    currentPublic:hex(current.current?.publicKey),currentGeneration:current.current?.generation??null});setPassword('');setConsent(false);
  },Boolean(session&&supported));
 }
 function restore(){if(!consent||!decoded.current||!target.current||!current.current)return;
  const active=decoded.current,actor=target.current,radio=current.current;
  run(async(signal)=>{try{const result=await restoreRadioIdentityBackup(session,active,actor,radio,{operations,consent:true,signal});if(alive.current){setOutcome(result);close();}}
   finally{if(decoded.current===active){clearRadioIdentityBackup(active);decoded.current=null;target.current=null;current.current=null;if(alive.current){setMetadata(null);setConsent(false);setFile(null);}}}});
 }
 function verify(){run(async(signal,live)=>{await verifyRestoredRadioIdentity(session,outcome.expectation,{operations,signal});if(live())setOutcome(value=>({...value,state:'verified'}));});}
 const label=mode==='export'?text.export:text.restore;
 return <section className="device-radio-identities device-identity-restore" hidden={!visible&&!outcome}>
  <h3>{text.title}</h3><p>{text.help}</p>
  <div className="device-field"><label>{text.protocol}<select value={kind} disabled={working||Boolean(outcome)||Boolean(metadata)} onChange={event=>{close();setInfo(null);setKind(Number(event.target.value));}}><option value={1}>MeshCore · Ed25519</option><option value={2}>Meshtastic · X25519</option></select></label></div>
  {!supported&&<p>{text.unsupported}</p>}
  {info&&<div role="status">{info.present?<><p>{text.currentKey}: <code dir="ltr">{info.publicKey}</code></p><p>{text.generation}: {info.generation}</p></>:<p>{text.absent}</p>}</div>}
  <div className="device-backup-actions"><Button disabled={disabled||working||!session||!supported||Boolean(outcome)} onClick={read}>{text.read}</Button><Button disabled={disabled||working||!session||!supported||Boolean(outcome)||info?.present===false} onClick={()=>{close();setMode('export');}}>{text.export}</Button><Button disabled={disabled||working||Boolean(outcome)} onClick={()=>{close();setMode('restore');}}>{text.restore}</Button></div>
  {outcome&&<div role="status"><p>{outcome.state==='verified'?text.verified:outcome.state==='reboot-required'?restoration.reboot:restoration.unknown}</p><code dir="ltr">{outcome.expectation.publicKey}</code><div className="device-backup-actions"><Button disabled={disabled||working||!session||!supported||outcome.state==='verified'} onClick={verify}>{restoration.verify}</Button><Button disabled={working} onClick={()=>{setOutcome(null);setInfo(null);setError(null);}}>{restoration.clear}</Button></div></div>}
  {error&&!mode&&<p role="alert">{error==='error'?text.error:common[error]}</p>}
  {mode&&<dialog ref={dialog} className="device-backup-dialog" aria-label={label} onCancel={event=>{event.preventDefault();if(!working)close();}}><form onSubmit={submit}>
   <h2>{label}</h2><p>{text.help}</p>
   {metadata?<><dl><dt>{text.protocol}</dt><dd>{metadata.kind===1?'MeshCore':'Meshtastic'}</dd><dt>{text.sourceKey}</dt><dd><code dir="ltr">{metadata.publicKey}</code></dd><dt>{text.currentKey}</dt><dd><code dir="ltr">{metadata.currentPublic??restoration.noDevice}</code></dd><dt>{text.generation}</dt><dd>{metadata.currentGeneration??'—'} → {metadata.currentGeneration!==null?metadata.currentGeneration+1:'—'}</dd><dt>{text.backupAuthor}</dt><dd><code dir="ltr">{metadata.sourceAuthor}</code></dd></dl><p>{metadata.currentPublic===metadata.publicKey?text.same:text.different}</p></>:
    <>{mode==='restore'&&<div className="device-field"><label>{text.file}<input type="file" accept=".tmrf,application/octet-stream" disabled={working} onChange={event=>{setFile(event.target.files?.[0]??null);setPassword('');setError(null);}}/></label></div>}
    <p>{mode==='export'?common.exportHelp:common.importHelp}</p><div className="device-field"><label>{common.password}<input type="password" autoComplete={mode==='export'?'new-password':'current-password'} value={password} disabled={working} onChange={event=>setPassword(event.target.value)}/></label>{mode==='export'&&<small>{common.passwordHelp}</small>}</div>
    {mode==='export'&&<div className="device-field"><label>{common.confirmation}<input type="password" autoComplete="new-password" value={confirmation} disabled={working} onChange={event=>setConfirmation(event.target.value)}/></label></div>}</>}
   {(mode==='export'||metadata)&&<label><input type="checkbox" checked={consent} disabled={working} onChange={event=>setConsent(event.target.checked)}/>{mode==='export'?text.consentExport:text.consentRestore}</label>}
   {working&&<p role="status">{common.working}</p>}{error&&<p role="alert">{error==='error'?text.error:common[error]}</p>}
   <div className="device-backup-actions"><Button disabled={working} onClick={close}>{common.cancel}</Button>{metadata?<Button type="primary" disabled={working||!consent||!session||!supported||metadata.currentGeneration===null||metadata.currentGeneration===0xffffffff} onClick={restore}>{restoration.restore}</Button>:<Button type="primary" htmlType="submit" disabled={working||!password||mode==='restore'&&!file||mode==='export'&&(!consent||!confirmation)}>{mode==='export'?common.encrypt:restoration.preview}</Button>}</div>
  </form></dialog>}
 </section>;
}
