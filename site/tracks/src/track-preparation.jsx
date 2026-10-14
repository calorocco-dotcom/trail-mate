import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {OPERATION} from '../../device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {prepareSavedTrackDraft} from './prepare-saved-draft.js';
import {useTrackPreparationText} from '../../i18n/management-copy.jsx';
import {TrackPublish} from './track-publish.jsx';
import {decode} from '../../geocaching/src/protocol.js';

const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
export function TrackPreparation({session,operations=[],draft,directory,disabled=false,acquire,release}){
 const {locale,text}=useTrackPreparationText(),number=new Intl.NumberFormat(locale);
 const [busy,setBusy]=useState(false),[notice,setNotice]=useState(null),[result,setResult]=useState(null),[submitted,setSubmitted]=useState(false);
 const controller=useRef(),epoch=useRef(0);
 useEffect(()=>{epoch.current++;controller.current?.abort();setResult(null);setNotice(null);setSubmitted(false);},[session,draft?.id,draft?.version]);
 useEffect(()=>()=>{epoch.current++;controller.current?.abort();},[]);
 const supported=[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.TRANSFER_BEGIN,OPERATION.TRANSFER_CHUNK,
  OPERATION.TRANSFER_FINISH,draft?.attachmentCount||draft?.predecessor&&decode(decode(new Uint8Array(draft.predecessor),4352)[0],4096)[0]===2?OPERATION.TRACK_PACKAGE_SIGN_PREPARE:OPERATION.TRACK_PREPARE].every(op=>operations.includes(op));
 async function prepare(){
  if(busy||disabled||!session||!draft||!supported)return;
  const lease=acquire?.();if(!lease)return;
  const token=epoch.current;controller.current=new AbortController();setBusy(true);setResult(null);setSubmitted(false);setNotice({key:'verify'});
  try{
   const author=await proveDeviceAuthor(session,{signal:controller.current.signal});
   if(token!==epoch.current)return;
   if(controller.current.signal.aborted){setNotice({key:'paused'});return;}
   const prepared=await prepareSavedTrackDraft(session,draft.id,draft.version,author,{operations,signal:controller.current.signal,
    onProgress:progress=>{if(token!==epoch.current)return;
     setNotice({key:progress.phase,offset:progress.offset,total:progress.total});}});
   if(token===epoch.current){setResult(prepared);setNotice({key:'prepared'});}
  }catch(error){if(token===epoch.current)setNotice({key:error.name==='AbortError'?'paused':'error',code:error.code});}
  finally{setBusy(false);release?.(lease);}
 }
 async function copy(){try{await navigator.clipboard.writeText(hex(result.objectId));if(!submitted)setNotice({key:'copied'});}catch{setNotice({key:'copyFailed'});}}
 return <section className="track-preparation"><h3>{text.title}</h3><p>{text.intro}</p>
  {!supported&&<p>{text.unsupported}</p>}
  <div className="tracks-actions"><Button disabled={disabled||busy||!session||!draft||!supported} onClick={prepare}>{text.prepare}</Button>
   {busy&&<Button onClick={()=>controller.current?.abort()}>{text.pause}</Button>}</div>
  {notice&&<p role="status">{text[notice.key]??text.preparing}{notice.total>0&&<> · <bdi>{number.format(notice.offset)} / {number.format(notice.total)} B</bdi></>}{notice.key==='error'&&notice.code!=null&&<> · <bdi>{String(notice.code)}</bdi></>}</p>}{result&&<><label>{text.id}<input readOnly dir="ltr" value={hex(result.objectId)}/></label><Button onClick={copy}>{text.copy}</Button><small>{text.revision.replace('{revision}',number.format(result.revision))}</small><TrackPublish session={session} operations={operations} draft={draft} prepared={result} directory={directory} disabled={disabled||busy} acquire={acquire} release={release} onSubmitted={()=>{setSubmitted(true);setNotice(null);}}/></>}
 </section>;
}
