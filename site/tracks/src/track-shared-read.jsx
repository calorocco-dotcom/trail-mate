import React,{useEffect,useMemo,useRef,useState} from 'react';
import {createPortal} from 'react-dom';
import {Button} from 'animal-island-ui';
import {OPERATION} from '../../device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackSharedText} from '../../i18n/track-shared-labels.js';
import {TrackMetadataSummary} from './track-metadata.jsx';
import {createDeviceTrackReadSession} from './device-shared-read.js';
import {createIPTrackReadSession} from './ip-read-session.js';
import {readSharedTrack} from './shared-track-read.js';
import {trackId,trackShareLink,copyTrackReference,parseTrackReference} from './shared-id.js';
import {SharedTrackPreview} from './shared-track-preview.jsx';
import {RouteInstallPanel} from './route-install-panel.jsx';
import {SharedTrackBrowse} from './shared-track-browse.jsx';
import {querySharedTrackViewport} from './shared-track-viewport.js';
import {querySharedTrackRegion} from './shared-track-region-query.js';
import {createTrackRegionClient} from './region-client.js';
import {querySharedTrackPage} from './shared-track-query.js';
import {equal} from '../../geocaching/src/protocol.js';
import {createOwnedTrackMaintenancePlan} from './owned-track-maintenance.js';
import {saveTrackDraft} from './draft-store.js';
import {TrackPreparation} from './track-preparation.jsx';
import {TrackIPPublication} from './track-ip-publication.jsx';
import {trackMaintenanceText} from '../../i18n/track-maintenance-labels.js';
import {publicTrackLabels} from '../../i18n/public-track-labels.js';
import {readSharedTrackPackage} from './shared-track-package.js';
import {SharedTrackPackagePanel} from './shared-track-package-panel.jsx';
import {PackageInstallPanel} from './package-install-panel.jsx';
import {PackageArchiveExport} from './package-archive-export.jsx';
import {readTrackConflict} from './track-conflict.js';
import {TrackConflictPanel} from './track-conflict-panel.jsx';
import {trackStorageErrorText} from '../../i18n/track-storage-labels.js';
import {readTrackVersionHistory} from './track-version-history.js';
import {TrackSharedHistory} from './track-shared-history.jsx';
import {queryTrackDirectories} from './multidirectory-query.js';
import {trackMultidirectoryText} from '../../i18n/track-multidirectory-labels.js';

const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
function initialReference(){return new URL(location.href).searchParams.has('id')?location.href:'';}
export function TrackSharedRead({session,operations=[],directory,queryDirectories=[],network,editorDraft,disabled=false,acquire,release,onImport,onOpenDraft,onDetail}){
 const locale=useManagementLocale(),text=useMemo(()=>trackSharedText(locale),[locale]),number=new Intl.NumberFormat(locale);
 const maintenance=useMemo(()=>trackMaintenanceText(locale),[locale]),[maintenanceDraft,setMaintenanceDraft]=useState(null),updateInput=useRef();
 const authorSupported=Boolean(session)&&[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE].every(operation=>operations.includes(operation));
 const supported=Boolean(network)||(authorSupported&&[OPERATION.TRACK_READ_BEGIN,OPERATION.TRACK_READ_STATUS,OPERATION.TRACK_READ_CHUNK,OPERATION.TRACK_READ_RELEASE].every(operation=>operations.includes(operation)));
 const createReader=options=>network?createIPTrackReadSession({...options,network}):createDeviceTrackReadSession(options);
 const [reference,setReference]=useState(initialReference),[detail,setDetail]=useState(null),[loading,setLoading]=useState(false),[notice,setNotice]=useState({key:'initial'}),[copyText,setCopyText]=useState(''),[url,setUrl]=useState('');
 const epoch=useRef(0),controller=useRef(),adapter=useRef(),running=useRef(false),importer=useRef(onImport);importer.current=onImport;
 const detailCallback=useRef(onDetail);detailCallback.current=onDetail;
 const regionContext=useRef();
 const authorSession=useRef(session);authorSession.current=session;
 const authorAttempt=useRef(),authorController=useRef(),requiresAuthor=useRef(false);
 const [identity,setIdentity]=useState(null);
 const [portal,setPortal]=useState(null);useEffect(()=>setPortal(document.getElementById('tracks-shared-detail')),[]);
 const verifiedAuthor=identity&&identity.session===session?identity.author:null;
 const owned=Boolean(detail&&verifiedAuthor&&equal(detail.record[1],verifiedAuthor.publicKey));
 const [browseGeneration,setBrowseGeneration]=useState(0);
 const [packet,setPacket]=useState(null),[packageError,setPackageError]=useState(null),[packageProgress,setPackageProgress]=useState(null);
 const [conflict,setConflict]=useState(null);
 const [history,setHistory]=useState(null),[historyError,setHistoryError]=useState(null),[historyReading,setHistoryReading]=useState(false);
 const availableDirectories=network?(queryDirectories.length?queryDirectories:directory?[directory]:[]):[];
 const sourceBindings=availableDirectories.map(value=>value.key+hex(value.publicKey)+hex(value.epoch)).sort().join('|');
 const detailDirectory=detail?.sourceDirectory??directory;
 useEffect(()=>{if(!network)return;const source=detail?.sourceDirectory??conflict?.sourceDirectory;if(source&&!availableDirectories.some(value=>value.key===source.key&&equal(value.publicKey,source.publicKey)&&equal(value.epoch,source.epoch))){epoch.current++;controller.current?.abort();setDetail(null);setConflict(null);setHistory(null);setHistoryError(null);setHistoryReading(false);running.current=false;setLoading(false);}},[sourceBindings,detail?.sourceDirectory,conflict?.sourceDirectory,network]);
 useEffect(()=>{if(editorDraft&&maintenanceDraft?.id===editorDraft.id&&maintenanceDraft.version!==editorDraft.version)setMaintenanceDraft(editorDraft);},[editorDraft,maintenanceDraft]);
 useEffect(()=>{detailCallback.current?.(detail??(supported&&directory?true:null));return()=>detailCallback.current?.(null);},[detail,supported,directory]);
 useEffect(()=>{
  epoch.current++;controller.current?.abort();adapter.current?.close();regionContext.current=undefined;setBrowseGeneration(value=>value+1);running.current=false;setLoading(false);setHistoryReading(false);setDetail(null);setConflict(null);setHistory(null);setHistoryError(null);setMaintenanceDraft(null);setCopyText('');setNotice({key:'initial'});
  return()=>{epoch.current++;controller.current?.abort();adapter.current?.close();};
 },[network?null:session,directory,network]);
 useEffect(()=>{authorAttempt.current=null;authorController.current?.abort();setIdentity(null);setMaintenanceDraft(null);if(requiresAuthor.current)controller.current?.abort();return()=>authorController.current?.abort();},[session]);
 useEffect(()=>{
  if(!authorSupported||disabled||authorAttempt.current===session)return;
  const lease=acquire?acquire():{};if(!lease)return;
  const target=session,abort=new AbortController();authorController.current=abort;authorAttempt.current=target;
  proveDeviceAuthor(target,{signal:abort.signal}).then(author=>{if(!abort.signal.aborted&&authorSession.current===target)setIdentity({session:target,author});}).catch(()=>{}).finally(()=>release?.(lease));
 },[session,authorSupported,disabled]);
 useEffect(()=>{
  if(!detail?.source){setUrl('');return;}
  const next=URL.createObjectURL(new Blob([detail.source],{type:'application/vnd.google-earth.kml+xml'}));setUrl(next);
  return()=>URL.revokeObjectURL(next);
 },[detail]);
 const share=detail?trackShareLink(new URL('../',location.href).href,detail.objectId,{revision:detail.revisionHash}):'';
 const selectedPacket=packet&&detail&&equal(packet.detail.objectId,detail.objectId)&&equal(packet.detail.revisionHash,detail.revisionHash)?packet:null;
 const plainPacket=useMemo(()=>detail?.source&&!(detail.record[0]===2&&detail.record[15])?{detail,attachmentIndex:null,files:[],ready:true}:null,[detail]);
 useEffect(()=>{setPacket(null);setPackageError(null);setPackageProgress(null);setHistory(null);setHistoryError(null);},[share]);
 const activeShare=useRef();activeShare.current=share;
 async function lookup(download=false,override,historical=false,source){
  const targetDirectory=source??(download?detailDirectory:directory);
  if(!supported||!targetDirectory||disabled||running.current)return;
  let target=override??(download?share:reference);
  const forceHistorical=historical||download&&detail?.current===false;
  if(override)setReference(override);
  try{parseTrackReference(target,{baseUrl:location.href});}catch{setNotice({key:'error',code:'TRACK_REFERENCE_INVALID'});return;}
  const lease=acquire?acquire():{};if(!lease)return;
  const token=++epoch.current,abort=new AbortController();controller.current=abort;requiresAuthor.current=!network;running.current=true;setLoading(true);setCopyText('');setNotice({key:network?'reading':'verifying'});
   if(!download){setDetail(null);setConflict(null);setHistory(null);setHistoryError(null);setMaintenanceDraft(null);}
  let reader;
  const current=()=>{if(token!==epoch.current||abort.signal.aborted)throw abort.signal.reason??new DOMException('Aborted','AbortError');};
  try{
   const author=network?null:await proveDeviceAuthor(session,{signal:abort.signal});current();if(author)setIdentity({session,author});
   reader=await createReader({session,directory:targetDirectory,author,operations,assertCurrent:current});adapter.current=reader;current();setNotice({key:'reading'});
   let evidence=null;
   if(network&&reader.capabilities.conflictSupported){
    evidence=await readTrackConflict(reader.request,target,{baseUrl:location.href,signal:abort.signal});current();setConflict(evidence?{proof:evidence,sourceDirectory:targetDirectory}:null);
    if(evidence&&!parseTrackReference(target,{baseUrl:location.href}).revision){setDetail(null);setMaintenanceDraft(null);setNotice({key:'ready'});return;}
   }
   const result=await readSharedTrack(reader.request,target,{baseUrl:location.href,signal:abort.signal,download,maxBytes:reader.capabilities.maxFileBytes,chunkBytes:reader.capabilities.chunkBytes,
    onProgress:progress=>{if(token===epoch.current)setNotice({key:'reading',offset:progress.offset,total:progress.total});}});
   current();setDetail({...result,...(evidence||forceHistorical?{current:false}:{}),sourceDirectory:targetDirectory});setMaintenanceDraft(null);setNotice({key:'ready'});
  }catch(error){if(token===epoch.current){if(error.code==='TRACK_AUTHOR_VERSION_CONFLICT'||error.code==='TRACK_CONFLICT_PROOF_INVALID'){setDetail(null);setMaintenanceDraft(null);setConflict({proof:null,invalid:error.code==='TRACK_CONFLICT_PROOF_INVALID'});}setNotice({key:error.name==='AbortError'?'cancelled':'error',code:error.code});}}
  finally{reader?.close();if(adapter.current===reader)adapter.current=undefined;if(token===epoch.current){running.current=false;setLoading(false);}release?.(lease);}
 }
 async function loadHistory(){
  if(!network||!detail||!detailDirectory||disabled||running.current)return;
  const lease=acquire?acquire():{};if(!lease)return;
  const token=++epoch.current,abort=new AbortController();controller.current=abort;requiresAuthor.current=false;running.current=true;setLoading(true);setHistoryReading(true);setHistory(null);setHistoryError(null);
  let reader;const current=()=>{if(token!==epoch.current||abort.signal.aborted)throw abort.signal.reason??new DOMException('Aborted','AbortError');};
  try{
   reader=await createIPTrackReadSession({network,directory:detailDirectory,assertCurrent:current});adapter.current=reader;
   const result=await readTrackVersionHistory(reader.request,detail,{signal:abort.signal,onProgress:value=>{if(token===epoch.current)setHistory(value);}});current();setHistory(result);
  }catch(error){if(token===epoch.current){setHistory(value=>value?{...value,status:'interrupted',complete:false}:null);setHistoryError(error.name==='AbortError'?null:error.code??error.name);}}
  finally{reader?.close();if(adapter.current===reader)adapter.current=undefined;if(token===epoch.current){running.current=false;setLoading(false);setHistoryReading(false);}release?.(lease);}
 }
 async function loadPackage(){
  if(!network||!detail||!detailDirectory||disabled||running.current)return;
  const lease=acquire?acquire():{};if(!lease)return;
  const token=++epoch.current,abort=new AbortController();controller.current=abort;requiresAuthor.current=false;running.current=true;setLoading(true);setPacket(null);setPackageError(null);setPackageProgress(null);
  let reader;const current=()=>{if(token!==epoch.current||abort.signal.aborted)throw abort.signal.reason??new DOMException('Aborted','AbortError');};
  try{
   reader=await createIPTrackReadSession({network,directory:detailDirectory,assertCurrent:current});adapter.current=reader;
   const result=await readSharedTrackPackage(reader.request,share,{baseUrl:location.href,signal:abort.signal,chunkBytes:reader.capabilities.chunkBytes,maxBytes:reader.capabilities.maxFileBytes,packageSupported:reader.capabilities.packageSupported,onProgress:value=>{if(token===epoch.current)setPackageProgress(value);}});
   current();const selected={...result.detail,...(conflict?.proof||detail.current===false?{current:false}:{}),sourceDirectory:detailDirectory};setDetail(selected);setPacket({...result,detail:selected});setPackageProgress(null);
  }catch(error){if(token===epoch.current){setPacket(null);setPackageProgress(null);if(error.name==='AbortError'){setPackageError(null);setNotice({key:'cancelled'});}else setPackageError(error.code??error.name);}}
  finally{reader?.close();if(adapter.current===reader)adapter.current=undefined;if(token===epoch.current){running.current=false;setLoading(false);}release?.(lease);}
 }
 async function browse(bounds,{signal,regionAction,filter={},ownedPage=false,continuation,multiple=false,retryFailed=false,onProgress}={}){
  if(!supported||!directory||disabled||running.current||((ownedPage||filter.mine)&&!authorSupported))return;
  const lease=acquire?acquire():{};if(!lease)return;
  const needAuthor=!network||ownedPage||filter.mine;
  const token=++epoch.current,abort=new AbortController();controller.current=abort;requiresAuthor.current=needAuthor;running.current=true;setLoading(true);setNotice({key:needAuthor?'verifying':'reading'});
  const cancel=()=>abort.abort(signal.reason??new DOMException('Aborted','AbortError'));signal?.addEventListener('abort',cancel,{once:true});if(signal?.aborted)cancel();
  const current=()=>{if(token!==epoch.current||abort.signal.aborted)throw abort.signal.reason??new DOMException('Aborted','AbortError');};
  let reader;
  try{
   const author=needAuthor?await proveDeviceAuthor(session,{signal:abort.signal}):null;current();if(author)setIdentity({session,author});
   if(network&&multiple&&(!regionAction||regionAction.type==='query')){
    const settings={author:filter.mine||ownedPage?author.publicKey:null,state:filter.archived?null:0,activity:filter.activity??null,tags:filter.tags??[]},scope=['track.multidirectory.query',ownedPage?'owner':regionAction?'region':'viewport',regionAction?[regionAction.region.id,regionAction.region.version,regionAction.region.bounds]:bounds,settings.author,settings.state,settings.activity,settings.tags];
    const result=await queryTrackDirectories(async(source,options)=>{
     let part;try{
      current();part=await createIPTrackReadSession({network,directory:source,assertCurrent:current});
      if(regionAction){
       const client=await createTrackRegionClient(part.request,{signal:abort.signal,metadataSupported:part.capabilities.metadataRegionSupported});
       if(!equal(client.capabilities.version,regionAction.region.version)||!regionAction.region.id.startsWith(client.capabilities.namespace+':'))throw Object.assign(Error('Region catalogue differs'),{code:'TRACK_MULTIDIRECTORY_REGION_MISMATCH'});
       return await querySharedTrackRegion(client,regionAction.region,{...settings,signal:abort.signal,limit:16,maxPages:1,maxItems:16,continuation:options.continuation,scopeId:hex(source.publicKey)+hex(source.epoch)});
      }
      return await querySharedTrackViewport(part.request,ownedPage?[-90,-180,90,180]:bounds,{...settings,signal:abort.signal,limit:16,maxPages:1,maxItems:16,continuation:options.continuation,scopeId:hex(source.publicKey)+hex(source.epoch)});
     }finally{part?.close();}
    },availableDirectories,scope,{signal:abort.signal,continuation:ownedPage?continuation?.continuation??null:continuation??null,retryFailed,onProgress:value=>{current();onProgress?.(ownedPage?{...value,cursor:value.continuation,author:author.publicKey.slice(),state:settings.state,activity:settings.activity,tags:settings.tags}:value);}});
    current();setNotice({key:'initial'});return ownedPage?{...result,cursor:result.continuation,author:author.publicKey.slice(),state:settings.state,activity:settings.activity,tags:settings.tags}:result;
   }
   reader=await createReader({session,directory,author,operations,assertCurrent:current,regionContext:regionContext.current});adapter.current=reader;regionContext.current=reader.regionContext;current();setNotice({key:'reading'});
   let result;
   if(ownedPage){
    const state=filter.archived?null:0;
    if(continuation&&(!equal(continuation.author,author.publicKey)||continuation.state!==state||continuation.activity!==(filter.activity??null)||JSON.stringify(continuation.tags)!==JSON.stringify(filter.tags??[])))throw Object.assign(Error('TRACK_QUERY_CURSOR_MISMATCH'),{code:'TRACK_QUERY_CURSOR_MISMATCH'});
    const page=await querySharedTrackPage(reader.request,{box:[-1800000000,-900000000,1799999999,900000000],author:author.publicKey,state,activity:filter.activity??null,tags:filter.tags??[],limit:Math.min(16,reader.capabilities.pageLimit),snapshot:continuation?.snapshot??null,cursor:continuation?.cursor??null,signal:abort.signal});
    result={...page,author:author.publicKey.slice(),state,activity:filter.activity??null,tags:(filter.tags??[]).slice(),truncated:false};
   }else if(regionAction){
    const client=await createTrackRegionClient(reader.request,{signal:abort.signal,metadataSupported:reader.capabilities.metadataRegionSupported===true});current();
    const options={signal:abort.signal};
    if(regionAction.type==='children')result=await client.children(regionAction.parent??null,{...options,after:regionAction.after??null});
    else if(regionAction.type==='select'){
     const selected=await client.region(regionAction.id,options);current();
     const page=selected.childCount?await client.children(selected,options):{items:[],cursor:null};result={selected,...page};
    }else if(regionAction.type==='query'){
     result=await querySharedTrackRegion(client,regionAction.region,{...options,author:filter.mine?author.publicKey:null,state:filter.archived?null:0,activity:filter.activity??null,tags:filter.tags??[],continuation:continuation??null,scopeId:hex(directory.publicKey)+hex(directory.epoch),limit:Math.min(16,reader.capabilities.pageLimit)});current();
    }else throw Object.assign(Error('TRACK_REGION_ACTION_INVALID'),{code:'TRACK_REGION_ACTION_INVALID'});
   }else result=await querySharedTrackViewport(reader.request,bounds,{signal:abort.signal,author:filter.mine?author.publicKey:null,state:filter.archived?null:0,activity:filter.activity??null,tags:filter.tags??[],continuation:continuation??null,scopeId:hex(directory.publicKey)+hex(directory.epoch),limit:Math.min(16,reader.capabilities.pageLimit)});
   current();setNotice({key:'initial'});return result;
  }finally{signal?.removeEventListener('abort',cancel);reader?.close();if(adapter.current===reader)adapter.current=undefined;if(token===epoch.current){running.current=false;setLoading(false);}release?.(lease);}
 }
 async function copy(value){
  const token=epoch.current,result=await copyTrackReference(value);
  if(token!==epoch.current)return;
  setCopyText(result.copied?'':result.text);setNotice({key:result.copied?'copied':'selectCopy'});
 }
 async function maintain(archive,kml,target=share){
  if(!owned||!detail?.current||detail.record[6]!==0||disabled||running.current||target!==activeShare.current)return;
  const lease=acquire?acquire():{};if(!lease)return;
  const token=++epoch.current,abort=new AbortController();controller.current=abort;requiresAuthor.current=true;running.current=true;setLoading(true);setNotice({key:'verifying'});let reader;
  const current=()=>{if(token!==epoch.current||abort.signal.aborted)throw abort.signal.reason??new DOMException('Aborted','AbortError');};
  try{
   const author=await proveDeviceAuthor(session,{signal:abort.signal});current();
   if(!equal(detail.record[1],author.publicKey))throw Object.assign(Error('TRACK_AUTHOR_CHANGED'),{code:'TRACK_AUTHOR_CHANGED'});
   reader=await createReader({session,directory:detailDirectory,author,operations,assertCurrent:current});adapter.current=reader;current();setNotice({key:'reading'});
   const result=await createOwnedTrackMaintenancePlan(reader.request,target,author,{archive,kml,signal:abort.signal,maxBytes:reader.capabilities.maxFileBytes,chunkBytes:reader.capabilities.chunkBytes,packageSupported:reader.capabilities.packageSupported===true});current();reader.close();
   const draft=await saveTrackDraft(result.source,{name:detail.record[9],filename:trackId(result.objectId)+'.kml',publicationPlan:result.plan,predecessor:result.previousSigned,archive,packageSource:result.packageSource});current();setMaintenanceDraft(draft);setNotice({key:'ready'});
  }catch(error){if(token===epoch.current)setNotice({key:error.name==='AbortError'?'cancelled':'error',code:error.code});}
  finally{reader?.close();if(adapter.current===reader)adapter.current=undefined;if(token===epoch.current){running.current=false;setLoading(false);}release?.(lease);}
 }
 async function updateFile(event){
  const file=event.target.files?.[0],target=share;event.target.value='';if(!file)return;
  if(file.size>8388608){setNotice({key:'error',code:'TRACK_FILE_TOO_LARGE'});return;}
  try{const source=new Uint8Array(await file.arrayBuffer());if(target===activeShare.current)await maintain(false,source,target);}catch{setNotice({key:'error',code:'TRACK_FILE_READ_FAILED'});}
 }
 async function saveCopy(){
  if(!detail?.source||!importer.current||disabled||running.current)return;
  if(detail.record[0]===2&&detail.record[15]&&!selectedPacket?.ready)return;
  const lease=acquire?acquire():{};if(!lease)return;
  running.current=true;setLoading(true);const token=epoch.current;
  try{const verified=selectedPacket?.detail??detail;await importer.current(new File([verified.source],trackId(verified.objectId)+'.kml',{type:'application/vnd.google-earth.kml+xml'}),{packageSource:{signed:[verified.manifest,verified.signature],kml:verified.source,attachmentIndex:selectedPacket?.attachmentIndex,attachmentFiles:selectedPacket?.files??[]}});}
  catch(error){if(token===epoch.current)setNotice({key:'error',code:error.code});}
  finally{if(token===epoch.current){running.current=false;setLoading(false);}release?.(lease);}
 }
 return <section className="track-shared-read" aria-label={text.title}>
  <h4>{text.title}</h4>
  <p className="tracks-help">{!supported&&session?text.unsupported:!directory?text.chooseDirectory:text.initial}</p>
  {network&&<p className="tracks-help">{publicTrackLabels[locale][!authorSupported?0:verifiedAuthor?1:2]}</p>}
  <label>{text.reference}<input aria-label={text.reference} dir="ltr" value={reference} disabled={disabled||loading} onChange={event=>setReference(event.target.value)} autoComplete="off" spellCheck={false}/></label>
  <div className="tracks-actions"><Button disabled={!supported||!directory||disabled||loading||!reference.trim()} onClick={()=>lookup(false)}>{text.find}</Button>{loading&&<Button onClick={()=>controller.current?.abort()}>{text.cancel}</Button>}</div>
   <p role="status">{notice.key==='error'&&trackStorageErrorText(locale,notice.code)||<>{text[notice.key]}{notice.total!=null&&<> · <bdi>{number.format(notice.offset)} / {number.format(notice.total)} B</bdi></>}{notice.key==='error'&&notice.code&&<> · <bdi>{String(notice.code)}</bdi></>}</>}</p>
  {copyText&&<textarea aria-label={text.selectCopy} dir="ltr" readOnly value={copyText} onFocus={event=>event.target.select()}/>}
  {portal&&conflict&&createPortal(<TrackConflictPanel state={conflict} selected={Boolean(detail)} selectedChainVerified={history?.complete===true} disabled={disabled||loading} onCopy={copy} onOpen={version=>lookup(false,trackShareLink(new URL('../',location.href).href,version.objectId,{revision:version.revisionHash}),false,conflict.sourceDirectory??directory)}/>,portal)}
   {portal&&supported&&directory&&createPortal(<SharedTrackBrowse key={hex(directory.delivery)+hex(directory.epoch)+browseGeneration} sourceScope={sourceBindings} directoryCount={availableDirectories.length} multipleMetadataSupported={availableDirectories.some(value=>network?.directories.get(value.key)?.operations?.includes(15))} multipleMetadataRegionSupported={availableDirectories.some(value=>network?.directories.get(value.key)?.operations?.includes(16))} metadataSupported={network?.directories.get(directory.key)?.operations?.includes(15)===true} metadataRegionSupported={network?.directories.get(directory.key)?.operations?.includes(16)===true} canUseAuthor={Boolean(verifiedAuthor)} authorKey={verifiedAuthor?.fingerprint??null} onQuery={browse} onRegion={(action,options)=>browse(null,{...options,regionAction:action})} disabled={disabled||loading} onOpen={item=>lookup(false,trackShareLink(new URL('../',location.href).href,item.objectId,{revision:item.revisionHash}),false,item.sourceDirectory)}/>,portal)}
  {portal&&detail&&createPortal(<article className="shared-track-detail">
   <h4><bdi>{detail.record[9]}</bdi></h4><p>{detail.record[6]===1?text.archived:text.active}</p>
   {network&&detail.sourceDirectory&&<p className="track-detail-source">{trackMultidirectoryText(locale).sources}: <bdi>{detail.sourceDirectory.name??detail.sourceDirectory.key}</bdi> · <code dir="ltr">{hex(detail.sourceDirectory.delivery)}</code></p>}
   <dl><dt>{text.id}</dt><dd><code dir="ltr">{trackId(detail.objectId)}</code></dd><dt>{text.revision}</dt><dd><bdi>{number.format(detail.record[4])}</bdi> · <code dir="ltr">{hex(detail.revisionHash)}</code></dd><dt>{text.author}</dt><dd><code dir="ltr">{hex(detail.record[1])}</code></dd></dl>
    <p><bdi>{detail.record[10]}</bdi></p><TrackMetadataSummary record={detail.record}/>
    <SharedTrackPreview detail={detail} packet={selectedPacket}/>
   {detail.record[0]===2&&detail.record[15]&&<SharedTrackPackagePanel packet={selectedPacket} onRead={loadPackage} supported={Boolean(network)} disabled={disabled||loading} error={packageError} progress={packageProgress}/>}
   <div className="tracks-actions"><Button disabled={loading} onClick={()=>copy(trackId(detail.objectId))}>{text.copyId}</Button><Button disabled={loading} onClick={()=>copy(share)}>{text.copyLink}</Button>
    {!detail.source?<Button disabled={disabled||loading||!directory} onClick={()=>lookup(true)}>{text.download}</Button>:url&&<a className="top-link" href={url} download={trackId(detail.objectId)+'.kml'}>{text.download}</a>}
    {detail.source&&onImport&&<Button disabled={disabled||loading||Boolean(detail.record[0]===2&&detail.record[15]&&!selectedPacket?.ready)} onClick={saveCopy}>{text.saveCopy}</Button>}
   </div><p className="tracks-help">{text.copyHelp}</p>
   {plainPacket&&<PackageArchiveExport packet={plainPacket} disabled={disabled||loading}/>}
   {network&&<TrackSharedHistory state={history} error={historyError} selectedHash={detail.revisionHash} disabled={disabled||loading} reading={historyReading} onRead={loadHistory} onCancel={()=>controller.current?.abort()} onCopy={copy} onOpen={version=>lookup(false,trackShareLink(new URL('../',location.href).href,version.objectId,{revision:version.revisionHash}),true,detailDirectory)}/>}
   {owned&&!conflict&&detail.current&&detail.record[6]===0&&<section className="track-maintenance"><p>{maintenance.help}</p><div className="tracks-actions"><Button disabled={disabled||loading} onClick={()=>updateInput.current.click()}>{maintenance.update}</Button><input ref={updateInput} hidden type="file" accept=".kml,application/vnd.google-earth.kml+xml" aria-label={maintenance.update} onChange={updateFile}/><Button disabled={disabled||loading} onClick={()=>maintain(true)}>{maintenance.archive}</Button></div></section>}
   {maintenanceDraft&&<><p role="status">{maintenance.saved}</p>{onOpenDraft&&<Button disabled={loading} onClick={()=>onOpenDraft(maintenanceDraft)}>{maintenance.open}</Button>}{network?<TrackIPPublication session={session} operations={operations} draft={maintenanceDraft} directory={detailDirectory} network={network} disabled={disabled||loading} acquire={acquire} release={release} onOpenDraft={onOpenDraft}/>:<TrackPreparation session={session} operations={operations} draft={maintenanceDraft} directory={detailDirectory} disabled={disabled||loading} acquire={acquire} release={release}/>}</>}
  </article>,portal)}
  {portal&&session&&createPortal(operations.includes(100)||detail?.record[0]===2?<PackageInstallPanel session={session} operations={operations} packet={selectedPacket??(detail?.source&&!(detail.record[0]===2&&detail.record[15])?{detail,attachmentIndex:null,files:[],ready:true}:null)} disabled={disabled||loading} acquire={acquire} release={release}/>:<RouteInstallPanel session={session} operations={operations} source={detail?.source} disabled={disabled||loading} acquire={acquire} release={release}/>,portal)}
 </section>;
}
