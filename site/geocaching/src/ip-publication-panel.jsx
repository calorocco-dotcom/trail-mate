import React,{useEffect,useRef,useState,useId} from 'react';
import {Button,Card} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {geocachingPublicationText} from '../../i18n/geocaching-publication-labels.js';
import {geocachingIpPublicationText} from '../../i18n/geocaching-ip-publication-labels.js';
import {geocachingDraftText} from '../../i18n/geocaching-draft-labels.js';
import {geocachingReceiptText} from '../../i18n/geocaching-receipt-labels.js';
import {geocachingDeviceSyncText} from '../../i18n/geocaching-device-sync-labels.js';
import {syncIPPublicationToDevice} from './ip-publication-device-sync.js';
import {getGeocachingNetwork} from './web-network.js';
import {decode,equal} from './protocol.js';
import {loadIPPublication,discardIPReview,listIPPublicationArchives,restoreIPPublicationArchive} from './ip-publication-journal.js';
import {geocachingHistoryText} from '../../i18n/geocaching-history-labels.js';
import {reviewIPPublication,signIPPublication,publishIPPublication,retainIPPublicationReceipt} from './ip-publication-workflow.js';
import {loadPublicationCheckpoint} from './publication-journal.js';
import {deviceCacheId} from './publication-record.js';
import {PublicationPanel as LegacyPublicationPanel} from './publication-panel.jsx';
const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
const pending=record=>record&&(record.stage!=='confirmed'||record.deviceSync?.stage==='pending');
const legacyPending=record=>record&&['submitting','admitted','stopping','failed','unknown'].includes(record.stage);

export function IPPublicationPanel({reader,author,connected,operations,detail,dirty,busy,epoch,owner,onBusy,onLock,onConfirmed,onCatalog}){
 const locale=useManagementLocale(),text=geocachingPublicationText(locale),ip=geocachingIpPublicationText(locale),draftText=geocachingDraftText(locale),numbers=new Intl.NumberFormat(locale);
 const receiptText=geocachingReceiptText(locale);
 const syncText=geocachingDeviceSyncText(locale);
 const historyText=geocachingHistoryText(locale),[history,setHistory]=useState(null);
 const [record,setRecord]=useState(null),[legacy,setLegacy]=useState(null),[directories,setDirectories]=useState([]),[target,setTarget]=useState('');
 const [working,setWorking]=useState(false),[review,setReview]=useState(null),[message,setMessage]=useState(null),[storageError,setStorageError]=useState(false);
 const dialog=useRef(),publicIdInput=useRef(),titleId=useId(),mounted=useRef(true),activeWork=useRef(false);
 const [publicId,setPublicId]=useState('');
 const supported=[32,33,35,36,71,72,73].every(op=>operations.includes(op));
 const directory=directories.find(row=>row.key===target),network=getGeocachingNetwork();
 const update=next=>{setRecord(next);onLock(Boolean(pending(next)||legacyPending(legacy)));};
 useEffect(()=>()=>{mounted.current=false;if(activeWork.current){reader.current?.cancel();onBusy(false);}},[]);
 useEffect(()=>{
  setReview(null);setWorking(false);setStorageError(false);setHistory(null);
  if(!author?.verified)return;
  try{const saved=loadIPPublication(author.publicKey),old=loadPublicationCheckpoint(author.publicKey);setRecord(saved);setLegacy(old);onLock(Boolean(pending(saved)||legacyPending(old)));}
  catch{setStorageError(true);setMessage(text.storageError);onLock(true);}
 },[author?.fingerprint,connected]);
 useEffect(()=>{if(review){dialog.current?.showModal();dialog.current?.querySelector('h2')?.focus();}else dialog.current?.close();},[review]);
 useEffect(()=>{let current=true;setPublicId('');if(record){const fields=decode(record.raw,4096);deviceCacheId(fields[1],fields[2]).then(id=>{if(current)setPublicId(hex(id));}).catch(()=>{});}return()=>{current=false;};},[record]);
 useEffect(()=>{if(review&&(dirty||!connected||author?.generation!==review.authorGeneration))setReview(null);},[dirty,connected,author?.generation]);
 async function run(work,device=true){
  if(working||busy||(device&&(!connected||!author?.verified||!reader.current)))return;
  const token=epoch.current;activeWork.current=true;setWorking(true);onBusy(true);setMessage(null);
  try{await work(token);}catch(error){if(mounted.current&&token===epoch.current){
   try{if(author?.verified)update(loadIPPublication(author.publicKey));}catch{setStorageError(true);onLock(true);}
   setMessage(<>{text.error} <bdi>{error.code??error.name??'IP_PUBLICATION_ERROR'}</bdi></>);
  }}finally{activeWork.current=false;if(mounted.current&&token===epoch.current){setWorking(false);onBusy(false);}}
 }
 async function refreshDirectories(){await run(async token=>{
  const connection=getGeocachingNetwork();if(!connection?.ready()){setMessage(ip.offline);return;}
  const rows=await connection.directories();if(token!==epoch.current)return;
  if(!Array.isArray(rows)||rows.some(row=>!row.capabilitiesVerified||typeof row.key!=='string'||!/^[0-9a-f]{32}$/.test(row.key)))throw Error('Invalid IP directories');
  setDirectories(rows);setTarget(previous=>rows.some(row=>row.key===previous)?previous:(rows.find(row=>row.receiptSupported===true)??rows[0])?.key??'');
 },false);}
 async function check(){await run(async token=>{
  if(storageError||legacyPending(legacy)||pending(record)||dirty||!detail?.editable||!directory?.receiptSupported)throw Error('Publication preconditions');
  const snapshot=await reader.current.read(detail.draft.id,author);if(token!==epoch.current)return;
  if(BigInt(snapshot.draft.generation)!==BigInt(detail.draft.generation))throw Error('Draft changed');
  const next=await reviewIPPublication(reader.current,snapshot,author,directory,getGeocachingNetwork());
  if(token===epoch.current){update(next);setReview(next);}
 });}
 async function submit(){await run(async token=>{
  if(!record||legacyPending(legacy)||storageError)throw Error('Retained publication unavailable');
  setReview(null);let next=record;
  if(['reviewed','signing'].includes(next.stage)){
   next=await signIPPublication(reader.current,next,author,{operations});if(token!==epoch.current)return;update(next);
  }
  const result=await publishIPPublication(reader.current,next,author,getGeocachingNetwork());if(token!==epoch.current)return;
  let completed=result.record;update(completed);
  if([32,33,35,75,76].every(op=>operations.includes(op))){
   completed=await retainIPPublicationReceipt(completed,author,getGeocachingNetwork());if(token!==epoch.current)return;update(completed);
   completed=await syncIPPublicationToDevice(reader.current,completed,author,{operations});if(token!==epoch.current)return;update(completed);setMessage(syncText.confirmed);
  }else setMessage(result.cleanupError?ip.cleanupError:ip.confirmed);
  onConfirmed(completed.draftId);
  try{const catalog=await reader.current.list(author);if(token===epoch.current)onCatalog(catalog);}catch{}
 });}
 async function discard(){await run(async token=>{await discardIPReview(record);if(token===epoch.current){update(null);setReview(null);}});}
 async function retainReceipt(){await run(async token=>{const next=await retainIPPublicationReceipt(record,author,getGeocachingNetwork());if(token===epoch.current){update(next);setMessage(receiptText.retained);}},false);}
 async function synchronize(){await run(async token=>{
  const next=await syncIPPublicationToDevice(reader.current,record,author,{operations});
  if(token!==epoch.current)return;update(next);setMessage(syncText.confirmed);onConfirmed(next.draftId);
  try{const catalog=await reader.current.list(author);if(token===epoch.current)onCatalog(catalog);}catch{}
 });}
 async function readHistory(offset=0){await run(async token=>{
  const page=listIPPublicationArchives(author.publicKey,{offset});if(token===epoch.current)setHistory(page);
 },false);}
 async function openHistory(item){await run(async token=>{
  const restored=await restoreIPPublicationArchive(author.publicKey,item.operationId);
  if(token!==epoch.current)return;update(restored);setHistory(listIPPublicationArchives(author.publicKey));
 },false);}
 async function copyPublicId(){if(!publicId)return;try{await navigator.clipboard.writeText(publicId);setMessage(text.copied);}catch{publicIdInput.current?.focus();publicIdInput.current?.select();setMessage(document.execCommand('copy')?text.copied:text.copyManual);}}
 const shown=record?decode(record.raw,4096):null,preview=review?decode(review.raw,4096):null;
 const currentSync=Boolean(connected&&author?.verified&&record&&equal(record.authorKey,author.publicKey)&&(record.deviceSync?.generation??record.authorGeneration)===author.generation);
 return <Card className="geo-publication-panel">
  <h3>{text.title}</h3><p>{ip.intro}</p>
  <section className="geo-device-directories"><h4>{ip.directories}</h4>
   <Button disabled={busy||working} onClick={refreshDirectories}>{ip.refresh}</Button>
   {!network?.ready()&&<p>{ip.offline}</p>}
   {directories.length>0&&<label className="geo-editor-field">{text.directory}<select aria-label={text.directory} value={target} disabled={busy||working||Boolean(pending(record))} onChange={event=>setTarget(event.target.value)}>
    {directories.map(row=><option key={row.key} value={row.key}>{row.name||row.key}</option>)}
   </select></label>}
  </section>
  {!supported&&<p>{text.unsupported}</p>}
  {legacy&&<section>{legacyPending(legacy)&&<p>{ip.legacy}</p>}<LegacyPublicationPanel maintenanceOnly reader={reader} author={author} connected={connected} operations={operations} detail={detail} dirty={dirty} busy={busy||working} epoch={epoch} owner={owner} onBusy={onBusy} onConfirmed={onConfirmed} onCatalog={onCatalog} onLock={locked=>{setLegacy(loadPublicationCheckpoint(author.publicKey));onLock(Boolean(locked||pending(record)));}}/></section>}
  {directory&&!directory.receiptSupported&&<p role="status">{receiptText.upgrade}</p>}
  <Button type="primary" disabled={!supported||storageError||legacyPending(legacy)||!connected||busy||working||dirty||!detail?.editable||!directory?.receiptSupported||Boolean(pending(record))} onClick={check}>{text.review}</Button>
  {dirty&&<p>{text.saveFirst}</p>}
  {record&&<section className="geo-publication-task" aria-label={text.retained}>
   <strong><bdi>{shown[8]}</bdi></strong><p role="status">{ip[record.stage]}</p>
   <p>{text.versionChange} <bdi>{numbers.format(shown[3]-1)} → {numbers.format(shown[3])}</bdi></p>
   <code className="geo-fingerprint" dir="ltr">{hex(record.operationId)}</code>
   {publicId&&<><label className="geo-editor-field">{text.publicId}<input ref={publicIdInput} dir="ltr" readOnly value={publicId}/></label><Button onClick={copyPublicId}>{text.copy}</Button></>}
   {record.stage==='confirmed'&&(record.receipt?<p>{record.deviceSync?.stage==='confirmed'&&currentSync?syncText.confirmed:receiptText.retained}</p>:<Button disabled={!author?.verified||!connected||busy||working||!network?.ready()} onClick={retainReceipt}>{receiptText.save}</Button>)}
   {record.receipt&&!(record.deviceSync?.stage==='confirmed'&&currentSync)&&<>
    {record.deviceSync?.stage==='pending'&&currentSync&&<p role="status">{syncText.pending}</p>}
    <Button disabled={!connected||!author?.verified||busy||working||storageError||![32,33,35,75,76].every(op=>operations.includes(op))} onClick={synchronize}>{syncText.sync}</Button>
   </>}
   {record.stage!=='confirmed'&&<div className="geo-maintenance-actions">
    <Button disabled={!supported||!connected||busy||working||dirty||storageError||legacyPending(legacy)} onClick={()=>record.stage==='reviewed'?setReview(record):submit()}>{ip.resume}</Button>
    {record.stage==='reviewed'&&<Button disabled={busy||working} onClick={discard}>{ip.discard}</Button>}
   </div>}
  </section>}
  <section aria-label={historyText.title}><h4>{historyText.title}</h4>
   <Button disabled={!author?.verified||busy||working||storageError} onClick={()=>readHistory()}>{historyText.refresh}</Button>
   {history?.rows.map(item=>{const fields=decode(item.raw,4096);return <div key={hex(item.operationId)} className="geo-maintenance-actions">
    <div><span><bdi>{fields[8]}</bdi> · {text.versionChange} {numbers.format(fields[3])}</span>
     <p><time dateTime={new Date(fields[15]*1000).toISOString()}>{new Intl.DateTimeFormat(locale,{dateStyle:'medium',timeStyle:'short'}).format(new Date(fields[15]*1000))}</time></p>
     <p>{item.deviceSync?.stage==='confirmed'&&connected&&author?.verified&&(item.deviceSync.generation??item.authorGeneration)===author.generation?syncText.confirmed:item.receipt?receiptText.retained:receiptText.save}</p>
    </div>
    <Button disabled={busy||working||storageError||Boolean(pending(record))||legacyPending(legacy)} onClick={()=>openHistory(item)}>{historyText.open}</Button>
   </div>;})}
   {history?.next!==null&&history?.next!==undefined&&<Button disabled={busy||working} onClick={()=>readHistory(history.next)}>{historyText.next}</Button>}
  </section>
  {message&&<p role="status">{message}</p>}
  {working&&<Button onClick={()=>reader.current?.cancel()}>{text.pause}</Button>}
  <dialog className="geo-publication-dialog" ref={dialog} aria-labelledby={titleId} onCancel={()=>setReview(null)}>
   <h2 id={titleId} tabIndex={-1}>{text.confirmTitle}</h2>
   {preview&&<><strong><bdi>{preview[8]}</bdi></strong><p>{text.directory}: <bdi>{directories.find(row=>row.key===review.directoryKey)?.name??review.directoryKey}</bdi></p>
    <p>{text.coordinates}: <bdi>{numbers.format(preview[6]/1e7)}, {numbers.format(preview[7]/1e7)}</bdi></p>
    <dl className="geo-publication-summary">
     <dt>{draftText.state}</dt><dd>{[draftText.active,draftText.disabled,draftText.archived][preview[5]]}</dd>
     <dt>{draftText.difficulty}</dt><dd>{numbers.format(preview[11]/2)}</dd>
     <dt>{draftText.terrain}</dt><dd>{numbers.format(preview[12]/2)}</dd>
     <dt>{draftText.size}</dt><dd>{[draftText.unknown,draftText.micro,draftText.small,draftText.regular,draftText.large,draftText.other][preview[13]]}</dd>
     <dt>{draftText.description}</dt><dd style={{whiteSpace:'pre-wrap',overflowWrap:'anywhere'}}><bdi>{preview[9]}</bdi></dd>
     <dt>{draftText.hint}</dt><dd style={{whiteSpace:'pre-wrap',overflowWrap:'anywhere'}}><bdi>{preview[10]}</bdi></dd>
    </dl>
    <p>{text.versionChange}: <bdi>{numbers.format(preview[3])}</bdi></p></>}
   <p>{ip.confirmHelp}</p><div className="geo-maintenance-actions"><Button onClick={()=>setReview(null)}>{text.back}</Button><Button type="primary" disabled={busy||working||!connected||dirty} onClick={submit}>{ip.publish}</Button></div>
  </dialog>
 </Card>;
}
