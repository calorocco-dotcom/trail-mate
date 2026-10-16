import {bytes,equal,integer,encode,decode} from '../../geocaching/src/protocol.js';
import {encodeTrackPublicationPlan,decodeTrackPublicationPlan} from './publication-plan.js';
import {withIPPublicationLock} from '../../geocaching/src/ip-publication-journal.js';
import {withTrackDraftLock} from './draft-lock.js';
const stages=['reviewed','signing','signed','publishing','unconfirmed','confirmed'];
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const base64=value=>btoa(Array.from(value,byte=>String.fromCharCode(byte)).join(''));
const key=record=>'trailmate.track.ip.v1.'+hex(record.authorKey)+'.'+record.draftId+'.'+record.draftVersion;

export function encodeTrackIPCheckpoint(record){
 if(!record||!stages.includes(record.stage)||typeof record.draftId!=='string'||!record.draftId.length||record.draftId.length>64||!integer(record.draftVersion,1,Number.MAX_SAFE_INTEGER)||
    !bytes(record.authorKey,64)||!bytes(record.requestId,16)||!record.requestId.some(Boolean)||!bytes(record.directoryKey,64)||!bytes(record.delivery,16)||!bytes(record.epoch,16)||!record.epoch.some(Boolean))throw Error('Invalid track IP checkpoint');
 const plan=encodeTrackPublicationPlan(record.plan),manifest=decode(record.plan.manifest,4096);
 if(!equal(manifest[1],record.authorKey))throw Error('Track IP checkpoint author differs');
 const signature=record.signature??null,receipt=record.receipt??null;
 if(signature!==null&&!bytes(signature,64)||stages.indexOf(record.stage)>=2&&signature===null||stages.indexOf(record.stage)<2&&signature!==null)throw Error('Track IP checkpoint requires signature');
 if(receipt!==null&&(!(receipt instanceof Uint8Array)||!receipt.length||receipt.length>1200)||record.stage==='confirmed'&&receipt===null||record.stage!=='confirmed'&&receipt!==null)throw Error('Invalid track IP receipt checkpoint');
 const raw=encode([1,stages.indexOf(record.stage),record.draftId,record.draftVersion,record.authorKey,record.requestId,record.directoryKey,record.delivery,record.epoch,plan,signature,receipt]);
 if(raw.length>6144)throw Error('Track IP checkpoint exceeds budget');return raw;
}

export function decodeTrackIPCheckpoint(raw){
 const row=decode(raw,6144);
 if(!Array.isArray(row)||row.length!==12||row[0]!==1||!integer(row[1],0,stages.length-1))throw Error('Invalid track IP checkpoint schema');
 const record={stage:stages[row[1]],draftId:row[2],draftVersion:row[3],authorKey:row[4],requestId:row[5],directoryKey:row[6],delivery:row[7],epoch:row[8],plan:decodeTrackPublicationPlan(row[9]),signature:row[10],receipt:row[11]};
 if(!equal(encodeTrackIPCheckpoint(record),raw))throw Error('Noncanonical track IP checkpoint');return record;
}

export function loadTrackIPCheckpoint(authorKey,draftId,draftVersion,{storage=localStorage}={}){
 if(!bytes(authorKey,64)||typeof draftId!=='string'||!integer(draftVersion,1,Number.MAX_SAFE_INTEGER))throw Error('Invalid track IP checkpoint selection');
 const text=storage.getItem(key({authorKey,draftId,draftVersion}));if(text===null)return null;
 if(typeof text!=='string'||text.length>8192)throw Error('Track IP checkpoint exceeds budget');
 const raw=Uint8Array.from(atob(text),value=>value.charCodeAt(0));if(base64(raw)!==text)throw Error('Invalid track IP checkpoint encoding');
 const record=decodeTrackIPCheckpoint(raw);
 if(!equal(record.authorKey,authorKey)||record.draftId!==draftId||record.draftVersion!==draftVersion)throw Error('Track IP checkpoint selection differs');return record;
}

// A local editor version is not a new shared object. Its latest confirmed
// publication supplies the signed parent when the same draft is edited again.
export function loadPreviousTrackIPCheckpoint(authorKey,draftId,draftVersion,{storage=localStorage}={}){
 if(!bytes(authorKey,64)||typeof draftId!=='string'||!draftId||!integer(draftVersion,1,Number.MAX_SAFE_INTEGER))throw Error('Invalid previous track publication selection');
 const prefix='trailmate.track.ip.v1.'+hex(authorKey)+'.'+draftId+'.';let selected=null;
 for(let index=0;index<storage.length;index++){
  const name=storage.key(index);if(typeof name!=='string'||!name.startsWith(prefix))continue;
  const suffix=name.slice(prefix.length);if(!/^[1-9][0-9]*$/.test(suffix))continue;
  const version=Number(suffix);if(!integer(version,1,Number.MAX_SAFE_INTEGER)||version>=draftVersion)continue;
  const candidate=loadTrackIPCheckpoint(authorKey,draftId,version,{storage});
  if(candidate?.stage==='confirmed'&&(!selected||version>selected.draftVersion))selected=candidate;
 }
 return selected;
}

export function saveTrackIPCheckpoint(record,{storage=localStorage,expected=null}={}){
 const raw=encodeTrackIPCheckpoint(record),name=key(record),existing=storage.getItem(name);
 if(expected){
  if(key(expected)!==name||existing!==base64(encodeTrackIPCheckpoint(expected)))throw Object.assign(Error('Track publication changed in another tab'),{code:'TRACK_IP_CHECKPOINT_STALE'});
  if(!equal(encodeTrackPublicationPlan(expected.plan),encodeTrackPublicationPlan(record.plan))||!equal(expected.requestId,record.requestId)||!equal(expected.directoryKey,record.directoryKey)||!equal(expected.delivery,record.delivery)||!equal(expected.epoch,record.epoch)||
     expected.signature&&!equal(expected.signature,record.signature)||expected.receipt&&!equal(expected.receipt,record.receipt))throw Error('Frozen track publication binding changed');
  const allowed={reviewed:['reviewed','signing'],signing:['signing','signed'],signed:['signed','publishing'],publishing:['publishing','unconfirmed','confirmed'],unconfirmed:['unconfirmed','publishing'],confirmed:['confirmed']};
  if(!allowed[expected.stage].includes(record.stage))throw Error('Invalid track publication transition');
 }else if(existing!==null)throw Object.assign(Error('Retained track publication must be resumed'),{code:'TRACK_IP_RETAINED'});
 storage.setItem(name,base64(raw));return decodeTrackIPCheckpoint(raw);
}

export function assertTrackIPDraftEditable(draftId,draftVersion,{storage=localStorage}={}){
 const suffix='.'+draftId+'.'+draftVersion;
 for(let index=0;index<storage.length;index++){
  const name=storage.key(index);if(typeof name!=='string'||!name.startsWith('trailmate.track.ip.v1.')||!name.endsWith(suffix))continue;
  const text=storage.getItem(name);if(typeof text!=='string'||text.length>8192)throw Error('Invalid retained track publication');
  const raw=Uint8Array.from(atob(text),value=>value.charCodeAt(0)),record=decodeTrackIPCheckpoint(raw);
  if(base64(raw)!==text||key(record)!==name||record.draftId!==draftId||record.draftVersion!==draftVersion)throw Error('Retained track publication binding differs');
  if(record.stage!=='confirmed')throw Object.assign(Error('Resolve or discard the retained track publication before editing this draft'),{code:'TRACK_IP_DRAFT_LOCKED'});
 }
}

export async function discardTrackIPReview(record,options={}){
 const frozen=decodeTrackIPCheckpoint(encodeTrackIPCheckpoint(record));
 return withTrackDraftLock(frozen.draftId,()=>withIPPublicationLock(frozen.authorKey,()=>{
  const storage=options.storage??localStorage,current=loadTrackIPCheckpoint(frozen.authorKey,frozen.draftId,frozen.draftVersion,{storage});
  if(!current||current.stage!=='reviewed'||current.signature!==null||!equal(encodeTrackIPCheckpoint(current),encodeTrackIPCheckpoint(frozen)))throw Error('Only the unchanged unsigned track review can be discarded');
  storage.removeItem(key(current));
 },options),options);
}

export function listTrackIPCheckpoints(authorKey,{storage=localStorage,offset=0,limit=20,pendingOnly=false}={}){
 if(!bytes(authorKey,64)||!integer(offset,0,100000)||!integer(limit,1,100)||typeof pendingOnly!=='boolean')throw Error('Invalid track publication history query');
 const prefix='trailmate.track.ip.v1.'+hex(authorKey)+'.',items=[];
 for(let index=0;index<storage.length;index++){
  const name=storage.key(index);if(typeof name!=='string'||!name.startsWith(prefix))continue;
  const text=storage.getItem(name);if(typeof text!=='string'||text.length>8192)throw Error('Invalid track publication history entry');
  const raw=Uint8Array.from(atob(text),value=>value.charCodeAt(0)),record=decodeTrackIPCheckpoint(raw);
  if(base64(raw)!==text||key(record)!==name||!equal(record.authorKey,authorKey))throw Error('Track publication history binding changed');
  if(pendingOnly&&record.stage==='confirmed')continue;
  const updated=decode(record.plan.manifest,4096)[8];
  items.push({name,updated});
 }
 items.sort((left,right)=>right.updated-left.updated||left.name.localeCompare(right.name));
 const rows=items.slice(offset,offset+limit).map(({name})=>decodeTrackIPCheckpoint(Uint8Array.from(atob(storage.getItem(name)),value=>value.charCodeAt(0))));
 return {rows,total:items.length,offset,next:offset+rows.length<items.length?offset+rows.length:null};
}
