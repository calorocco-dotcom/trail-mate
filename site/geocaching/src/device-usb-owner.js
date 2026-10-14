import {bytes,equal,integer} from './protocol.js';
import {managementOwner} from './publication-journal.js';
const hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
const fail=code=>{throw Object.assign(Error(code),{code});};

// Correlation only: never a login credential or a device private key. Save it
// after proving the connected author, before any draft/signature upload. A
// storage failure must not silently substitute a new owner and strand uploads.
export async function deviceUsbOwner(author,{preferred,storage=localStorage,locks=globalThis.navigator?.locks,random}={}){
 if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff)||
    (preferred!==undefined&&(!bytes(preferred,16)||!preferred.some(Boolean))))fail('GEO_SAVE_CHECKPOINT_INVALID');
 if(!locks?.request)fail('GEO_SAVE_LOCK_UNAVAILABLE');
 const key='trailmate.geo.device-usb-owner.v1.'+hex(author.publicKey)+'.'+author.generation;
 return locks.request(key,{mode:'exclusive'},()=>{
  let saved;try{saved=storage.getItem(key);}catch{fail('GEO_SAVE_STORAGE');}
  let owner;
  if(saved!==null){
   if(typeof saved!=='string'||!/^[a-f0-9]{32}$/.test(saved))fail('GEO_SAVE_CHECKPOINT_INVALID');
   owner=Uint8Array.from(saved.match(/../g),part=>parseInt(part,16));
   if(!owner.some(Boolean))fail('GEO_SAVE_CHECKPOINT_INVALID');
   if(preferred&&!equal(preferred,owner))fail('GEO_SAVE_OWNER_CONFLICT');
  }else{
   try{owner=preferred?.slice()??managementOwner(storage,random);}catch{fail('GEO_SAVE_STORAGE');}
   try{storage.setItem(key,hex(owner));if(storage.getItem(key)!==hex(owner))fail('GEO_SAVE_STORAGE');}catch{fail('GEO_SAVE_STORAGE');}
  }
  return owner.slice();
 });
}
