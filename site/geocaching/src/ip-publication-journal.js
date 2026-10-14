import {bytes,integer,decode,encode,equal} from './protocol.js';
const stages=new Set(['reviewed','signing','signed','publishing','unconfirmed','confirmed']);
const hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
const key=author=>'trailmate.geo.ip.v1.'+hex(author);
const b64=value=>btoa(Array.from(value,b=>String.fromCharCode(b)).join(''));
function binary(value,maximum){
 if(typeof value!=='string'||value.length>Math.ceil(maximum/3)*4)throw Error('Invalid publication binary checkpoint');
 let raw;try{raw=Uint8Array.from(atob(value),c=>c.charCodeAt(0));}catch{throw Error('Invalid publication binary checkpoint');}
 if(raw.length>maximum||b64(raw)!==value)throw Error('Invalid publication binary checkpoint');return raw;
}
function serial(record){
 if(!record||!stages.has(record.stage)||!bytes(record.authorKey,64)||!integer(record.authorGeneration,1,0xffffffff)||
    !bytes(record.draftId,16)||!bytes(record.operationId,16)||!record.operationId.some(b=>b)||!bytes(record.transferId,16)||!record.transferId.some(b=>b)||
    !bytes(record.hash,32)||!(record.raw instanceof Uint8Array)||!record.raw.length||record.raw.length>4096||
    typeof record.directoryKey!=='string'||!(/^[0-9a-f]{32}$/).test(record.directoryKey))throw Error('Invalid IP publication checkpoint');
 const raw=decode(record.raw,4096);
 const generation=BigInt(record.draftGeneration??0);if(generation<1n||generation>0xffffffffffffffffn)throw Error('Invalid publication draft generation');
 if(!Array.isArray(raw)||raw.length!==16||raw[0]!==1||!equal(raw[1],record.authorKey)||!equal(raw[2],record.draftId))throw Error('IP publication content binding mismatch');
 const signature=record.signature??null;
 if(signature!==null&&!bytes(signature,64))throw Error('Invalid publication signature checkpoint');
 if(['signed','publishing','unconfirmed','confirmed'].includes(record.stage)&&signature===null)throw Error('Publication checkpoint requires signature');
 let receipt;
 if(record.receipt!==undefined){
  const p=record.receipt;
  if(record.stage!=='confirmed'||!(p?.raw instanceof Uint8Array)||!p.raw.length||p.raw.length>256||!bytes(p.signature,64)||!bytes(p.publicKey,64)||!bytes(p.delivery,16)||!bytes(p.epoch,16))throw Error('Invalid directory receipt checkpoint');
  const fields=decode(p.raw,256);
  if(!Array.isArray(fields)||fields.length!==8||fields[0]!==1||!equal(encode(fields),p.raw)||!equal(fields[1],p.delivery)||hex(p.delivery)!==record.directoryKey||
     !equal(fields[2],p.epoch)||!bytes(fields[3],32)||fields[4]!==raw[3]||!bytes(fields[5],32)||fields[6]!==raw[5]||!integer(fields[7],1,253402300799))throw Error('Directory receipt checkpoint binding mismatch');
  receipt={raw:b64(p.raw),signature:b64(p.signature),publicKey:b64(p.publicKey),delivery:b64(p.delivery),epoch:b64(p.epoch)};
 }
 let deviceSync;
 if(record.deviceSync!==undefined){
  const sync=record.deviceSync;
  if(!receipt||!['pending','confirmed'].includes(sync?.stage)||!bytes(sync.id,16)||!sync.id.some(Boolean))throw Error('Invalid device synchronization checkpoint');
  if(sync.generation!==undefined&&!integer(sync.generation,1,0xffffffff))throw Error('Invalid device synchronization generation');
  deviceSync={stage:sync.stage,id:b64(sync.id),...(sync.generation===undefined?{}:{generation:sync.generation})};
 }
 return JSON.stringify({version:1,stage:record.stage,authorKey:b64(record.authorKey),authorGeneration:record.authorGeneration,draftId:b64(record.draftId),draftGeneration:String(generation),
  operationId:b64(record.operationId),transferId:b64(record.transferId),hash:b64(record.hash),raw:b64(record.raw),signature:signature&&b64(signature),directoryKey:record.directoryKey,...(receipt?{receipt}:{}),...(deviceSync?{deviceSync}:{})});
}
export function loadIPPublication(author,{storage=localStorage}={}){
 if(!bytes(author,64))throw Error('Invalid publication author');
 const text=storage.getItem(key(author));if(text===null)return null;
 if(text.length>8192)throw Error('Publication checkpoint exceeds limit');
 let value;try{value=JSON.parse(text);}catch{throw Error('Publication checkpoint unreadable');}
 if(!value||value.version!==1)throw Error('Unsupported publication checkpoint');
 const record={stage:value.stage,authorKey:binary(value.authorKey,64),authorGeneration:value.authorGeneration,draftId:binary(value.draftId,16),draftGeneration:BigInt(value.draftGeneration),
  operationId:binary(value.operationId,16),transferId:binary(value.transferId,16),hash:binary(value.hash,32),raw:binary(value.raw,4096),
  signature:value.signature===null?null:binary(value.signature,64),directoryKey:value.directoryKey};
 if(value.receipt!==undefined){const p=value.receipt;if(!p||typeof p!=='object')throw Error('Invalid directory receipt checkpoint');record.receipt={raw:binary(p.raw,256),signature:binary(p.signature,64),publicKey:binary(p.publicKey,64),delivery:binary(p.delivery,16),epoch:binary(p.epoch,16)};}
 if(value.deviceSync!==undefined)record.deviceSync={stage:value.deviceSync?.stage,id:binary(value.deviceSync?.id,16),...(value.deviceSync?.generation===undefined?{}:{generation:value.deviceSync.generation})};
 if(!equal(record.authorKey,author)||serial(record)!==text)throw Error('Publication checkpoint binding or schema changed');
 return record;
}
export function saveIPPublication(record,{storage=localStorage,expected=null}={}){
 const text=serial(record),existing=storage.getItem(key(record.authorKey));
 if(expected){if(existing!==serial(expected))throw Object.assign(Error('Publication checkpoint changed in another tab'),{code:'PUBLICATION_CHECKPOINT_STALE'});}
 else if(existing!==null){
  const previous=loadIPPublication(record.authorKey,{storage});
  if(previous.stage!=='confirmed')throw Object.assign(Error('Resolve retained publication first'),{code:'PUBLICATION_RETAINED'});
  // Preserve signed content and any unsynchronized proof before replacing the
  // active slot. A storage failure leaves the previous active record intact.
  const archiveKey=key(previous.authorKey)+'.archive.'+hex(previous.operationId);
  const archived=storage.getItem(archiveKey);
  if(archived!==null&&archived!==serial(previous))throw Error('Publication archive binding changed');
  storage.setItem(archiveKey,serial(previous));
 }
 storage.setItem(key(record.authorKey),text);
 return loadIPPublication(record.authorKey,{storage});
}
export async function withIPPublicationLock(author,work,{locks=globalThis.navigator?.locks}={}){
 if(!bytes(author,64)||!locks?.request)throw Error('Browser publication locking unavailable');
 const name='trailmate-geo-ip-'+hex(new Uint8Array(await crypto.subtle.digest('SHA-256',author)));
 return locks.request(name,{mode:'exclusive',ifAvailable:true},lock=>{
  if(!lock)throw Object.assign(Error('Another tab owns this publication'),{code:'PUBLICATION_BUSY'});return work();
 });
}
export async function discardIPReview(record,options={}){
 const author=record.authorKey.slice(),expected=serial(record);
 return withIPPublicationLock(author,()=>{
  const storage=options.storage??localStorage,stored=loadIPPublication(author,{storage});
  if(!stored||stored.stage!=='reviewed'||stored.signature!==null||serial(stored)!==expected)throw Error('Only the unchanged unsigned review can be discarded');
  storage.removeItem(key(author));
 },options);
}

export function listIPPublicationArchives(author,{storage=localStorage,offset=0,limit=20}={}){
 if(!bytes(author,64)||!integer(offset,0,100000)||!integer(limit,1,100))throw Error('Invalid publication history query');
 const prefix=key(author)+'.archive.',keys=[];
 for(let index=0;index<storage.length;index++){
  const name=storage.key(index);if(typeof name==='string'&&name.startsWith(prefix))keys.push(name);
 }
 const ordered=keys.map(name=>{
  const record=loadIPPublication(author,{storage:{getItem:()=>storage.getItem(name)}});
  if(!record||record.stage!=='confirmed'||name!==prefix+hex(record.operationId))throw Error('Invalid publication archive binding');
  const timestamp=decode(record.raw,4096)[15];
  if(!integer(timestamp,1,253402300799))throw Error('Invalid publication archive timestamp');
  return {name,timestamp};
 });
 ordered.sort((left,right)=>right.timestamp-left.timestamp||left.name.localeCompare(right.name));
 const rows=ordered.slice(offset,offset+limit).map(({name})=>loadIPPublication(author,{storage:{getItem:()=>storage.getItem(name)}}));
 return {rows,total:keys.length,offset,next:offset+rows.length<keys.length?offset+rows.length:null};
}

export async function restoreIPPublicationArchive(author,operationId,options={}){
 if(!bytes(author,64)||!bytes(operationId,16))throw Error('Invalid publication archive selection');
 const account=author.slice(),id=operationId.slice();
 return withIPPublicationLock(account,()=>{
  const storage=options.storage??localStorage,name=key(account)+'.archive.'+hex(id);
  const retained=loadIPPublication(account,{storage:{getItem:()=>storage.getItem(name)}});
  if(!retained||retained.stage!=='confirmed'||!equal(retained.operationId,id))throw Error('Publication archive unavailable');
  const current=loadIPPublication(account,{storage});
  if(current&&current.stage!=='confirmed')throw Object.assign(Error('Resolve retained publication first'),{code:'PUBLICATION_RETAINED'});
  if(current&&equal(current.operationId,id))return current;
  // Saving archives the current confirmed record before restoring the selected
  // one. Remove the selected archive only after the active write succeeded.
  const restored=saveIPPublication(retained,{storage});
  storage.removeItem(name);
  return restored;
 },options);
}
