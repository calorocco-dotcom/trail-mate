import {bytes,equal,integer,encode,decode} from '../../geocaching/src/protocol.js';
import {withIPPublicationLock} from '../../geocaching/src/ip-publication-journal.js';
import {encodeTrackIPCheckpoint,decodeTrackIPCheckpoint,loadTrackIPCheckpoint} from './ip-publication-journal.js';
import {confirmDeviceTrackPublication} from './device-ip-confirmation.js';
import {OPERATION} from '../../device/src/management-protocol.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const name=(record,generation)=>'trailmate.track.device-history.v1.'+hex(record.authorKey)+'.'+hex(record.plan.operationId)+'.'+generation;
function encodeHistoryState(state){
 if(!state||!bytes(state.checkpointHash,32)||!bytes(state.manifestId,16)||!state.manifestId.some(Boolean)||
    !bytes(state.receiptId,16)||!state.receiptId.some(Boolean)||equal(state.manifestId,state.receiptId)||
    !integer(state.generation,1,0xffffffff)||!integer(state.stage,0,2)||!integer(state.cleaned,0,3)||
    state.stage===0&&state.cleaned!==0||state.stage===2&&state.cleaned!==3)throw Error('Invalid track history restoration checkpoint');
 return encode([1,state.checkpointHash,state.manifestId,state.receiptId,state.generation,state.stage,state.cleaned]);
}
export async function readTrackDeviceHistoryState(record,{generation,storage=localStorage}={}){
 if(!integer(generation,1,0xffffffff)||record.stage!=='confirmed')throw Error('Confirmed track and current generation required');
 const raw=encodeTrackIPCheckpoint(record),hash=new Uint8Array(await crypto.subtle.digest('SHA-256',raw)),text=storage.getItem(name(record,generation));
 if(text===null)return null;
 if(typeof text!=='string'||text.length>320||!/^(?:[0-9a-f]{2})+$/.test(text))throw Error('Invalid retained history restoration');
 const encoded=Uint8Array.from(text.match(/../g),value=>parseInt(value,16)),row=decode(encoded,160);
 if(!Array.isArray(row)||row.length!==7||row[0]!==1)throw Error('Invalid retained history restoration schema');
 const state={checkpointHash:row[1],manifestId:row[2],receiptId:row[3],generation:row[4],stage:row[5],cleaned:row[6]};
 if(!equal(encodeHistoryState(state),encoded)||!equal(hash,state.checkpointHash)||state.generation!==generation)throw Error('History restoration bindings changed');
 return state;
}
export async function restoreTrackDeviceHistory(session,record,author,options={}){
 const frozen=decodeTrackIPCheckpoint(encodeTrackIPCheckpoint(record)),account={...author,publicKey:author.publicKey?.slice()};
 return withIPPublicationLock(frozen.authorKey,async()=>{
  if(!account.verified||!equal(account.publicKey,frozen.authorKey)||!integer(account.generation,1,0xffffffff))
   throw Object.assign(Error('Track author identity differs'),{code:'STALE_IDENTITY'});
  if(![32,33,35,OPERATION.TRANSFER_CLEANUP,OPERATION.TRACK_IMPORT_HISTORY].every(op=>options.operations?.includes(op)))throw Error('Device history restoration capability unavailable');
  const current=loadTrackIPCheckpoint(frozen.authorKey,frozen.draftId,frozen.draftVersion,options);
  if(!current||current.stage!=='confirmed'||!equal(encodeTrackIPCheckpoint(current),encodeTrackIPCheckpoint(frozen)))throw Error('Confirmed track history changed');
  const storage=options.storage??localStorage,key=name(current,account.generation),check=()=>{if(options.signal?.aborted)throw options.signal.reason??new DOMException('Paused','AbortError');};
  let state=await readTrackDeviceHistoryState(current,{generation:account.generation,storage});check();
  if(!state){const random=options.randomBytes??(size=>crypto.getRandomValues(new Uint8Array(size)));
   state={checkpointHash:new Uint8Array(await crypto.subtle.digest('SHA-256',encodeTrackIPCheckpoint(current))),
    manifestId:random(16).slice(),receiptId:random(16).slice(),generation:account.generation,stage:0,cleaned:0};
   storage.setItem(key,hex(encodeHistoryState(state)));}
  const save=next=>{if(storage.getItem(key)!==hex(encodeHistoryState(state)))throw Error('History restoration changed in another tab');
   storage.setItem(key,hex(encodeHistoryState(next)));state=next;};
  // Neither a completed import nor unfinished cleanup proves this newly
  // connected device has the head. Reverify, then clean BOTH reuploaded files.
  if(state.stage!==0)save({...state,stage:0,cleaned:0});
  if(state.stage===0){
   const result=await confirmDeviceTrackPublication(session,current,account,{...options,id:state.receiptId,manifestId:state.manifestId,importHistory:true});
   if(!result.durable)throw Error('Restored history is not durable');
   save({...state,stage:1});
  }
  const items=[[state.manifestId,4,current.plan.manifest.length,new Uint8Array(await crypto.subtle.digest('SHA-256',current.plan.manifest))],
   [state.receiptId,7,current.receipt.length,new Uint8Array(await crypto.subtle.digest('SHA-256',current.receipt))]];
  for(let index=0;index<items.length;index++){
   if(state.cleaned&(1<<index))continue;check();
   const [id,kind,total,hash]=items[index];options.onProgress?.({phase:'cleaning',item:index});
   const removed=await session.request(OPERATION.TRANSFER_CLEANUP,[id.slice(),kind,total,hash.slice()]);
   if(!Array.isArray(removed)||removed.length!==0)throw Error('Invalid history cleanup acknowledgement');
   save({...state,cleaned:state.cleaned|(1<<index)});
  }
  save({...state,stage:2});options.onProgress?.({phase:'complete'});return state;
 },options);
}
