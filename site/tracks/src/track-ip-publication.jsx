import React,{useEffect,useId,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackPreparationText} from '../../i18n/track-preparation-labels.js';
import {trackPublishText} from '../../i18n/track-publish-labels.js';
import {trackSharedText} from '../../i18n/track-shared-labels.js';
import {geocachingIpPublicationText} from '../../i18n/geocaching-ip-publication-labels.js';
import {geocachingDraftText} from '../../i18n/geocaching-draft-labels.js';
import {decode} from '../../geocaching/src/protocol.js';
import {loadTrackIPCheckpoint,discardTrackIPReview,listTrackIPCheckpoints} from './ip-publication-journal.js';
import {readTrackDraft,saveTrackDraft} from './draft-store.js';
import {restoreTrackIPHistoryFile} from './ip-history-restore.js';
import {geocachingHistoryText} from '../../i18n/geocaching-history-labels.js';
import {verifyTrackManifest} from './manifest.js';
import {verifyTrackReceipt} from './receipt.js';
import {kmlActionText} from '../../i18n/kml-action-labels.js';
import {reviewTrackIPPublication,signTrackIPPublication,publishTrackIPPublication} from './ip-publication-workflow.js';
import {TrackDeviceSync} from './track-device-sync.jsx';
import {trackAttachmentsText} from '../../i18n/track-attachments-labels.js';
import {trackStorageErrorText} from '../../i18n/track-storage-labels.js';
import {trackTransferText} from '../../i18n/track-transfer-labels.js';
import {syncTrackIPPublication} from './device-sync-workflow.js';
import {trackDeviceSyncText} from '../../i18n/track-device-sync-labels.js';
import {TrackShareControls} from './track-share-controls.jsx';

export function TrackIPPublication({session,operations=[],draft,directory,network,disabled=false,acquire,release,onOpenDraft}){
 const locale=useManagementLocale(),prep=trackPreparationText(locale),pub=trackPublishText(locale),shared=trackSharedText(locale),ip=geocachingIpPublicationText(locale),states=geocachingDraftText(locale),numbers=new Intl.NumberFormat(locale);
 const [record,setRecord]=useState(null),[working,setWorking]=useState(false),[review,setReview]=useState(false),[error,setError]=useState(null);
 const [progress,setProgress]=useState(null),[paused,setPaused]=useState(false);const transferText=trackTransferText(locale);
 const [history,setHistory]=useState(null),historyText=geocachingHistoryText(locale);
 const [restoredDraft,setRestoredDraft]=useState(null);
 const attachmentsText=trackAttachmentsText(locale);
 const syncText=trackDeviceSyncText(locale);
 const epoch=useRef(0),controller=useRef(),author=useRef(),dialog=useRef(),title=useId();
 const packageRequired=record?decode(record.plan.manifest,4096)[0]===2:Boolean(draft?.attachmentCount||draft?.predecessor&&decode(decode(new Uint8Array(draft.predecessor),4352)[0],4096)[0]===2);
 const signing=[32,33,35,packageRequired?99:96].every(op=>operations.includes(op)),identity=[1,2].every(op=>operations.includes(op));
 useEffect(()=>{epoch.current++;controller.current?.abort();setRecord(null);setHistory(null);setRestoredDraft(null);setReview(false);setWorking(false);setError(null);setProgress(null);setPaused(false);author.current=undefined;},[session,draft?.id,draft?.version,directory,network]);
 useEffect(()=>()=>{epoch.current++;controller.current?.abort();},[]);
 useEffect(()=>{if(review){dialog.current?.showModal();dialog.current?.querySelector('h2')?.focus();}else dialog.current?.close();},[review]);
 async function run(submit){
  if(working||disabled||!identity||!session||!draft||!directory||!network)return;
  const lease=acquire?acquire():{};if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;setWorking(true);setError(null);setPaused(false);setProgress({phase:'verify'});
  const onProgress=value=>{if(token===epoch.current&&!abort.signal.aborted)setProgress({phase:value.phase,kind:value.kind,assetId:value.assetId,offset:value.offset,total:value.total});};
  try{
   const account=await proveDeviceAuthor(session,{signal:abort.signal});if(token!==epoch.current)return;author.current=account;
   let next=submit?record:await reviewTrackIPPublication(draft.id,draft.version,account,directory,network,{signal:abort.signal});
   if(token!==epoch.current||abort.signal.aborted)return;setRecord(next);
   if(!submit){setReview(next.stage==='reviewed');return;}
   setReview(false);
   if(['reviewed','signing'].includes(next.stage)){
    next=await signTrackIPPublication(session,next,account,{operations,signal:abort.signal,onProgress});if(token!==epoch.current)return;setRecord(next);
   }
   next=await publishTrackIPPublication(network,next,account,{signal:abort.signal,onProgress});if(token!==epoch.current||abort.signal.aborted)return;setRecord(next);
   if(next.plan.generation===account.generation&&[32,33,35,37,97].every(op=>operations.includes(op))){
    setProgress({phase:'syncing'});await syncTrackIPPublication(session,next,account,{operations,signal:abort.signal});
    if(token===epoch.current&&!abort.signal.aborted){setRecord({...next});setProgress({phase:'confirmed'});}
   }
  }catch(failure){if(token===epoch.current){
   try{if(author.current)setRecord(loadTrackIPCheckpoint(author.current.publicKey,draft.id,draft.version));}catch{}
   if(failure.name==='AbortError'){setError(null);setPaused(true);}else setError(typeof failure.code==='string'?failure.code:failure.name??'TRACK_IP_PUBLICATION_ERROR');
  }}finally{if(token===epoch.current)setWorking(false);release?.(lease);}
 }
 const fields=record?decode(record.plan.manifest,4096):null,needsSignature=record&&['reviewed','signing'].includes(record.stage);
 const progressLabel=progress?.phase==='uploading'?transferText.kml:progress?.phase==='resources'?(progress.kind==='index'?transferText.index:transferText.resource):progress?.phase==='publishing'?transferText.publishing:progress?.phase==='syncing'?syncText.title:progress?.phase==='confirmed'?ip.confirmed:prep[progress?.phase]??prep.preparing;
 const hasBytes=Number.isSafeInteger(progress?.total)&&progress.total>0&&progress.total<=8388608&&Number.isSafeInteger(progress?.offset)&&progress.offset>=0&&progress.offset<=progress.total;
 async function discard(){if(working||disabled||!record)return;try{await discardTrackIPReview(record);setRecord(null);setReview(false);setError(null);}catch(failure){setError(failure.code??failure.name);}}
 async function readHistory(offset=0){
  if(working||disabled||!identity||!session)return;const lease=acquire?acquire():{};if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;setWorking(true);setError(null);setHistory(null);
  try{const account=await proveDeviceAuthor(session,{signal:abort.signal}),page=listTrackIPCheckpoints(account.publicKey,{offset});
   for(const item of page.rows){if(item.signature)await verifyTrackManifest([item.plan.manifest,item.signature],{expectedAuthor:account.publicKey});
    if(item.stage==='confirmed')await verifyTrackReceipt(decode(item.receipt,1200),{directoryPublicKey:item.directoryKey,directoryEpoch:item.epoch,requestId:item.requestId,manifestSigned:[item.plan.manifest,item.signature]});}
   if(token===epoch.current&&!abort.signal.aborted){author.current=account;setHistory(page);}
  }catch(failure){if(token===epoch.current)setError(failure.code??failure.name);}finally{if(token===epoch.current)setWorking(false);release?.(lease);}
 }
 async function openHistory(item){if(working||disabled)return;const token=epoch.current;try{const saved=await readTrackDraft(item.draftId);if(token!==epoch.current)return;if(saved.version!==item.draftVersion)throw Error('Historical draft version is unavailable');if(draft?.id===saved.id&&draft?.version===saved.version){setRecord(item);setReview(item.stage==='reviewed');}else await onOpenDraft?.(saved);}catch(failure){if(token===epoch.current)setError(failure.code??failure.name);}}
 async function restoreHistory(item){
  if(working||disabled||!network||!session)return;const lease=acquire?acquire():{};if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;setWorking(true);setError(null);
  try{const account=await proveDeviceAuthor(session,{signal:abort.signal});if(token!==epoch.current)return;
   if(!account.publicKey.every((value,index)=>value===item.authorKey[index]))throw Error('Track history belongs to another device');
   const restored=await restoreTrackIPHistoryFile(item,network,{baseUrl:new URL('../',location.href).href,signal:abort.signal});if(token!==epoch.current||abort.signal.aborted)return;
   const saved=await saveTrackDraft(restored.source,{name:restored.name});if(token===epoch.current){setRestoredDraft(saved);await onOpenDraft?.(saved);}
  }catch(failure){if(token===epoch.current)setError(failure.code??failure.name);}finally{if(token===epoch.current)setWorking(false);release?.(lease);}
 }
 return <section className="track-preparation"><h3>{pub.title}</h3><p>{ip.intro}</p>
  {!signing&&<p>{prep.unsupported}</p>}
  <Button disabled={disabled||working||!identity||!draft||!directory||!network} onClick={()=>run(false)}>{prep.prepare}</Button>
  {record&&<><p role="status">{ip[record.stage]}</p><strong><bdi>{fields[9]}</bdi></strong>
   <p>{shared.revision}: {numbers.format(fields[4])}</p>
   {record.stage!=='confirmed'&&<Button disabled={disabled||working||!identity||needsSignature&&!signing} onClick={()=>record.stage==='reviewed'?setReview(true):run(true)}>{ip.resume}</Button>}
   {record.stage==='reviewed'&&<Button disabled={disabled||working} onClick={discard}>{ip.discard}</Button>}
   {record.stage==='confirmed'&&<><TrackShareControls record={record} disabled={disabled||working}/><TrackDeviceSync record={record} session={session} operations={operations} disabled={disabled||working} acquire={acquire} release={release}/></>}
  </>}
  {error&&<p role="alert">{trackStorageErrorText(locale,error,{publication:record?.stage==='unconfirmed',resume:ip.resume})??(error==='TRACK_PACKAGE_DIRECTORY'?attachmentsText.directoryUnsupported:<>{prep.error} · <bdi>{error}</bdi></>)}</p>}
  {restoredDraft&&<p role="status">{kmlActionText(locale).saved} <bdi>{restoredDraft.name}</bdi></p>}
  {working&&<><p role="status" data-track-ip-progress={progress?.phase}><span>{progressLabel}</span>{progress?.assetId&&<> · <bdi>{progress.assetId}</bdi></>}{hasBytes&&<> · <bdi>{numbers.format(progress.offset)} / {numbers.format(progress.total)} B</bdi></>}</p>{hasBytes&&<progress aria-label={progressLabel} max={progress.total} value={progress.offset}/>}<Button onClick={()=>controller.current?.abort()}>{['uploading','resources'].includes(progress?.phase)?transferText.pause:progress?.phase==='publishing'?pub.pause:prep.pause}</Button></>}
  {paused&&<p role="status">{transferText.paused}</p>}
  <section aria-label={historyText.title}><h4>{historyText.title}</h4><Button disabled={disabled||working||!identity} onClick={()=>readHistory()}>{historyText.refresh}</Button>
   {history?.rows.map(item=>{const fields=decode(item.plan.manifest,4096);return <div key={item.draftId+':'+item.draftVersion}>
    <strong><bdi>{fields[9]}</bdi></strong><p>{ip[item.stage]} · {shared.revision}: {numbers.format(fields[4])}</p>
    <Button disabled={disabled||working||(!onOpenDraft&&draft?.id!==item.draftId)} onClick={()=>openHistory(item)}>{historyText.open}</Button>
    {item.signature&&<Button disabled={disabled||working||!network} onClick={()=>restoreHistory(item)}>{shared.saveCopy}</Button>}
    {item.stage==='confirmed'&&<TrackDeviceSync record={item} session={session} operations={operations} disabled={disabled||working} acquire={acquire} release={release}/>}
   </div>;})}
   {history?.next!=null&&<Button disabled={disabled||working} onClick={()=>readHistory(history.next)}>{historyText.next}</Button>}
  </section>
  <dialog className="geo-publication-dialog" ref={dialog} aria-labelledby={title} onCancel={()=>setReview(false)}>
   <h2 id={title} tabIndex={-1}>{pub.title}</h2>
   {fields&&<><strong><bdi>{fields[9]}</bdi></strong><p style={{whiteSpace:'pre-wrap',overflowWrap:'anywhere'}}><bdi>{fields[10]}</bdi></p>
    <dl><dt>{shared.author}</dt><dd><code style={{overflowWrap:'anywhere'}}>{author.current?.fingerprint}</code></dd>
     <dt>{ip.directories}</dt><dd><bdi>{directory?.name??directory?.key}</bdi></dd>
     <dt>{shared.revision}</dt><dd>{numbers.format(fields[4])}</dd><dt>{states.state}</dt><dd>{fields[6]===1?shared.archived:shared.active}</dd>
     <dt>KML</dt><dd>{numbers.format(fields[13][1])} B</dd>{fields[0]===2&&<><dt>{attachmentsText.title}</dt><dd>{numbers.format(fields[15]?.[2]??0)} · <bdi>{numbers.format(draft?.attachmentBytes??0)} B</bdi></dd></>}</dl></>}
   <p>{ip.confirmHelp}</p><div className="tracks-actions"><Button onClick={()=>setReview(false)}>{prep.pause}</Button><Button type="primary" disabled={disabled||working||!signing} onClick={()=>run(true)}>{ip.publish}</Button></div>
  </dialog>
 </section>;
}
