import {bytes,equal,integer,encode,decode} from '../../geocaching/src/protocol.js';
import {withIPPublicationLock} from '../../geocaching/src/ip-publication-journal.js';
import {encodeTrackIPCheckpoint,decodeTrackIPCheckpoint,loadTrackIPCheckpoint} from './ip-publication-journal.js';
import {confirmDeviceTrackPublication} from './device-ip-confirmation.js';
import {OPERATION} from '../../device/src/management-protocol.js';
import {cleanupTrackAttachmentUploads} from './device-attachment-cleanup.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const name=record=>'trailmate.track.device-sync.v1.'+hex(record.authorKey)+'.'+hex(record.plan.operationId);

function encodeSyncState(state){
 if(!state||!bytes(state.checkpointHash,32)||!bytes(state.transferId,16)||!state.transferId.some(Boolean)||
    !integer(state.stage,0,2)||!integer(state.cleaned,0,7)||state.stage===0&&state.cleaned!==0||state.stage===2&&state.cleaned!==7)
  throw Error('Invalid track device synchronization checkpoint');
 return encode([1,state.checkpointHash,state.transferId,state.stage,state.cleaned]);
}

export async function readTrackDeviceSyncState(record,{storage=localStorage}={}){
 const raw=encodeTrackIPCheckpoint(record);
 if(record.stage!=='confirmed')throw Error('Track device synchronization requires a confirmed directory receipt');
 const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',raw)),text=storage.getItem(name(record));
 if(text===null)return null;
 if(typeof text!=='string'||text.length>256||!/^(?:[0-9a-f]{2})+$/.test(text))throw Error('Invalid retained track device synchronization');
 const encoded=Uint8Array.from(text.match(/../g),value=>parseInt(value,16)),row=decode(encoded,128);
 if(!Array.isArray(row)||row.length!==5||row[0]!==1)throw Error('Invalid retained track device synchronization schema');
 const state={checkpointHash:row[1],transferId:row[2],stage:row[3],cleaned:row[4]};
 if(!equal(encodeSyncState(state),encoded)||!equal(hash,state.checkpointHash))throw Error('Retained track device synchronization binding changed');
 return state;
}

export async function syncTrackIPPublication(session,record,author,options={}){
 const frozen=decodeTrackIPCheckpoint(encodeTrackIPCheckpoint(record)),account={...author,publicKey:author.publicKey?.slice()};
 return withIPPublicationLock(frozen.authorKey,async()=>{
  if(!account.verified||!equal(account.publicKey,frozen.authorKey)||account.generation!==frozen.plan.generation)
   throw Object.assign(Error('Track device identity changed'),{code:'STALE_IDENTITY'});
  if(![32,33,35,OPERATION.TRANSFER_CLEANUP,97].every(op=>options.operations?.includes(op)))throw Error('Device track synchronization capability unavailable');
  const current=loadTrackIPCheckpoint(frozen.authorKey,frozen.draftId,frozen.draftVersion,options);
  if(!current||current.stage!=='confirmed'||!equal(encodeTrackIPCheckpoint(current),encodeTrackIPCheckpoint(frozen)))
   throw Object.assign(Error('Track publication checkpoint changed'),{code:'TRACK_IP_CHECKPOINT_STALE'});
  const storage=options.storage??localStorage,check=()=>{if(options.signal?.aborted)throw options.signal.reason??new DOMException('Paused','AbortError');};
  let state=await readTrackDeviceSyncState(current,{storage});check();
  // The fixed receipt transfer is persisted before the first USB write. Retrying
  // an uncertain confirmation reuses this binding and never signs/publishes again.
  if(!state){state={checkpointHash:new Uint8Array(await crypto.subtle.digest('SHA-256',encodeTrackIPCheckpoint(current))),
   transferId:(options.randomBytes??(size=>crypto.getRandomValues(new Uint8Array(size))))(16),stage:0,cleaned:0};
   storage.setItem(name(current),hex(encodeSyncState(state)));}
  const save=next=>{const existing=storage.getItem(name(current));if(existing!==hex(encodeSyncState(state)))throw Error('Track device synchronization changed in another tab');
   storage.setItem(name(current),hex(encodeSyncState(next)));state=next;};
  if(state.stage===2)return state;
  if(state.stage===0){
   const result=await confirmDeviceTrackPublication(session,current,account,{...options,id:state.transferId});
   if(!result.durable)throw Error('Device track receipt is not durable');
   // Do not remove any source until the durable acknowledgement is retained.
   save({...state,stage:1});
  }
  const items=[[current.plan.kmlUploadId,3,current.plan.fileBytes,current.plan.fileHash],
   [current.plan.manifestUploadId,4,current.plan.manifest.length,new Uint8Array(await crypto.subtle.digest('SHA-256',current.plan.manifest))],
   [state.transferId,7,current.receipt.length,new Uint8Array(await crypto.subtle.digest('SHA-256',current.receipt))]];
  for(let index=0;index<items.length;index++){
   if(state.cleaned&(1<<index))continue;check();
   const [id,kind,total,hash]=items[index];options.onProgress?.({phase:'cleaning',item:index});
   // Exact bound cleanup is idempotent even if the candidate has already been
   // deleted. It never reopens/creates an upload or touches durable track data.
   const removed=await session.request(OPERATION.TRANSFER_CLEANUP,[id.slice(),kind,total,hash.slice()]);
   if(!Array.isArray(removed)||removed.length!==0)throw Error('Invalid cleanup acknowledgement');
   save({...state,cleaned:state.cleaned|(1<<index)});
  }
  await cleanupTrackAttachmentUploads(session,current,options);check();
  save({...state,stage:2});options.onProgress?.({phase:'complete'});return state;
 },options);
}
