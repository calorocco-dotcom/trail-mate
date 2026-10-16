import React,{useEffect,useRef,useState,useId} from 'react';
import {Button,Card} from 'animal-island-ui';
import {OPERATION} from '../../device/src/management-protocol.js';
import {equal} from './protocol.js';
import {loadPublicationCheckpoint,savePublicationCheckpoint} from './publication-journal.js';
import {useGeocachingPublicationText,useGeocachingDraftText} from '../../i18n/management-copy.jsx';
import {ManagementLanguage} from '../../i18n/management-language.jsx';
const problem=key=>Object.assign(Error(key),{presentationKey:key});
const errorMessage=error=>({key:error.presentationKey??'error',detail:error.presentationKey?'':error.message});
const hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
const unresolved=record=>record&&['submitting','admitted','stopping','failed','unknown'].includes(record.stage);

export function PublicationPanel({reader,author,connected,operations,detail,dirty,directory,busy,epoch,owner,onBusy,onLock,onConfirmed,onCatalog,maintenanceOnly=false}){
  const {locale,text}=useGeocachingPublicationText(),{text:fields}=useGeocachingDraftText(),number=new Intl.NumberFormat(locale);
  const [record,setRecord]=useState(),[working,setWorking]=useState(false),[message,setMessage]=useState(null),[review,setReview]=useState(),[job,setJob]=useState();
  const feedback=message&&<>{text[message.key]}{message.detail&&<><br/><bdi>{message.detail}</bdi></>}</>;
  const current=useRef(),dialog=useRef(),publicId=useRef(),titleId=useId();
  const [storageError,setStorageError]=useState(false);
  const [stopReview,setStopReview]=useState(false);
  const supported=[OPERATION.CACHE_PREPARE,OPERATION.CACHE_COMMIT,OPERATION.JOB_GET].every(op=>operations.includes(op));
  function checkpoint(next){const fresh={...next,restored:false};savePublicationCheckpoint(fresh);current.current=fresh;setRecord(fresh);onLock(Boolean(unresolved(fresh)));}
  useEffect(()=>{
    setReview(undefined);setStopReview(false);setWorking(false);setJob(undefined);setStorageError(false);
    if(!author?.verified)return;
    try{const saved=loadPublicationCheckpoint(author.publicKey);current.current=saved;setRecord(saved);onLock(Boolean(unresolved(saved)));}
    catch(error){setMessage({key:'storageError',detail:error.message});setStorageError(true);onLock(true);}
  },[author?.fingerprint,connected]);
  useEffect(()=>{if(review){dialog.current?.showModal();dialog.current?.querySelector('h2')?.focus();}else dialog.current?.close();},[review]);
  useEffect(()=>{if(review&&(dirty||!detail||BigInt(detail.draft.generation)!==BigInt(review.prepared.generation)||
    !directory||!equal(directory.delivery,review.prepared.directory)||author?.generation!==review.prepared.authorGeneration))setReview(undefined);},[dirty,detail?.draft.generation,directory,author?.generation]);
  async function perform(work){
    if(working||busy||!connected||!author?.verified||!reader.current)return;
    const token=epoch.current;setWorking(true);onBusy(true);setMessage(null);
    try{await work(token);}catch(error){if(token===epoch.current)setMessage(errorMessage(error));}
    finally{if(token===epoch.current){setWorking(false);onBusy(false);}}
  }
  async function check(){await perform(async token=>{
    if(unresolved(current.current))throw problem('resolveRetained');
    if(!detail?.editable||dirty||!directory?.capabilitiesVerified)throw problem('preconditions');
    const snapshot=await reader.current.read(detail.draft.id,author);
    if(token!==epoch.current)return;
    if(BigInt(snapshot.draft.generation)!==BigInt(detail.draft.generation))throw problem('draftChanged');
    const prepared=await reader.current.prepare(snapshot,directory,author,{prepareId:crypto.getRandomValues(new Uint8Array(16))});
    if(token!==epoch.current)return;
    checkpoint({stage:'prepared',title:snapshot.draft.name,owner:owner.current.slice(),prepared});setJob(undefined);setReview({prepared,draft:snapshot.draft,directoryName:directory.name});
  });}
  async function submit(resume=false){await perform(async token=>{
    const retained=current.current;if(!retained)return;
    if(!resume&&performance.now()>=retained.prepared.expiresAt){setReview(undefined);throw problem('expired');}
    // Persist before the first potentially side-effecting serial request.
    checkpoint({...retained,stage:'submitting'});setReview(undefined);
    try{
      await reader.current.commit(retained.prepared,author,{resume});
      if(token!==epoch.current)return;
      checkpoint({...retained,stage:'admitted'});setMessage({key:'admittedMessage'});
    }catch(error){if(token===epoch.current){checkpoint({...retained,stage:error.submitted===false?'prepared':'unknown'});throw error;}}
  });}
  async function query(stop=false){await perform(async token=>{
    const retained=current.current;if(!retained)return;
    setStopReview(false);
    if(stop)checkpoint({...retained,stage:'stopping'});
    const binding={operationId:retained.prepared.prepareId,revision:retained.prepared.toRevision};
    const result=await (stop?reader.current.stop(binding,author):reader.current.job(binding,author));
    if(token!==epoch.current)return;
    setJob(result);
    const stage=result.phase===3?'confirmed':result.phase===4?'stopped':result.phase===5?'failed':
      result.phase===6||retained.stage==='stopping'?'stopping':'admitted';
    checkpoint({...retained,stage});
    setMessage({key:result.directoryConfirmed?'confirmedRefresh':result.phase===4?'stoppedMessage':'unconfirmedMessage',detail:result.directoryConfirmed||result.phase===4?'':result.error});
    if(result.directoryConfirmed){onConfirmed(retained.prepared.id);try{const catalog=await reader.current.list(author);if(token===epoch.current){onCatalog(catalog);setMessage({key:'confirmedRefreshed'});}}
      catch(error){if(token===epoch.current)setMessage({key:'refreshFailed',detail:error.message});}}
  });}
  async function copyPublicId(){if(!job?.cacheId)return;try{await navigator.clipboard.writeText(hex(job.cacheId));setMessage({key:'copied'});}
    catch{publicId.current?.focus();publicId.current?.select();setMessage({key:document.execCommand('copy')?'copied':'copyManual'});}}
  const sameDraft=record&&detail&&equal(record.prepared.id,detail.draft.id);
  return <Card className={maintenanceOnly?'geo-legacy-publication-panel':'geo-publication-panel'}>
    <h3>{text.title}</h3>
    {!maintenanceOnly&&<p>{text.intro}</p>}
    {!maintenanceOnly&&!supported&&<p>{text.unsupported}</p>}
    {!maintenanceOnly&&<Button type="primary" disabled={storageError||!supported||!connected||busy||working||dirty||!detail?.editable||!directory?.capabilitiesVerified||Boolean(unresolved(record))} onClick={check}>{text.review}</Button>}
    {dirty&&<p>{text.saveFirst}</p>}
    {record&&<section className="geo-publication-task" aria-label={text.retained}>
      <strong><bdi>{record.title}</bdi></strong><p role="status">{record.restored?text.lastRecord:''}{text[record.stage]??text.unknown}</p>
      <p>{text.versionChange} <bdi>{number.format(record.prepared.fromRevision)} → {number.format(record.prepared.toRevision)}</bdi></p>
      <small>{text.operationHelp}</small>
      <div className="geo-maintenance-actions"><Button disabled={!(maintenanceOnly?operations.includes(OPERATION.JOB_GET):supported)||!connected||busy||working||record.stage==='prepared'} onClick={()=>query()}>{text.query}</Button>
        {!maintenanceOnly&&['submitting','unknown'].includes(record.stage)&&<Button disabled={!supported||!connected||busy||working||record.prepared.authorGeneration!==author?.generation} onClick={()=>submit(true)}>{text.resume}</Button>}
        {unresolved(record)&&operations.includes(OPERATION.JOB_CANCEL)&&<Button disabled={!connected||busy||working||record.prepared.authorGeneration!==author?.generation} onClick={()=>setStopReview(true)}>{text.stop}</Button>}
      </div>
      {stopReview&&<div role="group" aria-label={text.confirmStop}>
        <p>{text.stopHelp}</p>
        <div className="geo-maintenance-actions"><Button disabled={busy||working} onClick={()=>setStopReview(false)}>{text.keep}</Button><Button disabled={!connected||busy||working} onClick={()=>query(true)}>{text.confirmLocalStop}</Button></div>
      </div>}
      {job?.cacheId&&<><label className="geo-editor-field">{text.publicId}<input dir="ltr" ref={publicId} readOnly value={hex(job.cacheId)}/></label><Button onClick={copyPublicId}>{text.copy}</Button></>}
      {sameDraft&&unresolved(record)&&<p>{text.resolveDraft}</p>}
    </section>}
    {message&&<p role="status">{feedback}</p>}
    {working&&<Button onClick={()=>reader.current?.cancel()}>{text.pause}</Button>}
    <dialog className="geo-publication-dialog" ref={dialog} aria-labelledby={titleId} onCancel={event=>{event.preventDefault();if(!working)setReview(undefined);}}>
      {review&&<><h2 id={titleId} tabIndex={-1}>{text.confirmTitle}</h2><ManagementLanguage page="geocaching"/><h3><bdi>{review.draft.name}</bdi></h3>
        <dl><dt>{fields.deviceAuthor}</dt><dd className="geo-fingerprint" dir="ltr">{author?.fingerprint}</dd>
          <dt>{text.directory}</dt><dd><bdi>{review.directoryName}</bdi><code className="geo-fingerprint" dir="ltr">{hex(review.prepared.directory)}</code></dd>
          <dt>{text.versionChange}</dt><dd><bdi>{number.format(review.prepared.fromRevision)} → {number.format(review.prepared.toRevision)}</bdi></dd>
          <dt>{text.coordinates}</dt><dd dir="ltr">{review.draft.latitudeE7/1e7}, {review.draft.longitudeE7/1e7}</dd>
          <dt>{fields.state}</dt><dd>{[fields.active,fields.disabled,fields.archived][review.draft.state]}</dd>
          <dt>{text.rating}</dt><dd><bdi>{number.format(review.draft.difficultyX2/2)} / {number.format(review.draft.terrainX2/2)}</bdi></dd>
          <dt>{fields.size}</dt><dd>{[fields.unknown,fields.micro,fields.small,fields.regular,fields.large,fields.other][review.draft.containerSize]}</dd>
        </dl><p className="geo-draft-text">{review.draft.description}</p><p>{fields.hint}：{review.draft.hint}</p>
        <p>{text.signatureHelp}</p>
        {message&&<p role="alert">{feedback}</p>}<div className="geo-maintenance-actions"><Button disabled={working} onClick={()=>setReview(undefined)}>{text.back}</Button><Button type="primary" disabled={working||!connected} onClick={()=>submit(false)}>{text.submit}</Button></div></>}
    </dialog>
  </Card>;
}
