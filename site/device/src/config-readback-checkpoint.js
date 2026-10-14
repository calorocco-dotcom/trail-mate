export const CONFIG_READBACK_STORAGE_KEY='trailmate.config.readback.v1';
const keys=['version','fingerprint','sha256','transferId'];
const hex=bytes=>Array.from(bytes,byte=>byte.toString(16).padStart(2,'0')).join('');
function valid(value){
 const shape=value?.version===2?[...keys,'contentSha256']:keys;
 return value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===shape.length&&
  Object.keys(value).every(key=>shape.includes(key))&&(value.version===1||value.version===2)&&
  (value.version===1||/^[0-9a-f]{64}$/.test(value.contentSha256))&&
  (value.fingerprint===null||/^[0-9a-f]{64}$/.test(value.fingerprint))&&
  /^[0-9a-f]{64}$/.test(value.sha256)&&/^[0-9a-f]{32}$/.test(value.transferId);
}
export function saveConfigurationReadbackCheckpoint(storage,check,hash,id){
 if(!(hash instanceof Uint8Array)||hash.length!==32||!(id instanceof Uint8Array)||id.length!==16)throw Error('CONFIG_READBACK_CHECKPOINT_INVALID');
 const value={version:check.contentSha256?2:1,fingerprint:check.fingerprint??null,sha256:hex(hash),transferId:hex(id),...(check.contentSha256?{contentSha256:check.contentSha256}:{})};
 if(!valid(value))throw Error('CONFIG_READBACK_CHECKPOINT_INVALID');
 // Only public author identity, document/content digests and operation ID.
 // Never serialize the in-memory expected document or individual field values.
 try{storage.setItem(CONFIG_READBACK_STORAGE_KEY,JSON.stringify(value));}
 catch(error){try{storage.removeItem(CONFIG_READBACK_STORAGE_KEY);}catch{}throw error;}
 return value;
}
export function loadConfigurationReadbackCheckpoint(storage){
 try{
  const raw=storage.getItem(CONFIG_READBACK_STORAGE_KEY);if(raw===null)return undefined;
  if(raw.length>512)throw Error('Invalid checkpoint');
  const value=JSON.parse(raw);if(!valid(value))throw Error('Invalid checkpoint');
  return value;
 }catch{
  try{storage.removeItem(CONFIG_READBACK_STORAGE_KEY);}catch{}
  return undefined;
 }
}

// A native f32 emission can differ textually from the submitted document.
// Replace the digest only after an in-memory, author-bound full-field match.
// Never replace another tab's newer transfer checkpoint.
export function confirmConfigurationReadbackCheckpoint(storage,check,result,hash){
 if(result?.status!=='matched'||!check?.expected||!check.fingerprint||!(check.transferId instanceof Uint8Array)||check.transferId.length!==16)return false;
 const current=loadConfigurationReadbackCheckpoint(storage);if(!current||current.fingerprint!==check.fingerprint||current.transferId!==hex(check.transferId))return false;
 saveConfigurationReadbackCheckpoint(storage,check,hash,check.transferId);return true;
}
