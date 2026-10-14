import {bytes,equal,integer} from './protocol.js';
import {validateDraftFields,encodeDraftEdit} from './device-draft.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const key=author=>'trailmate.geo.draft-save.v1.'+hex(author);
const fail=code=>{throw Object.assign(Error(code),{code});};
const parse=(value,size)=>{if(typeof value!=='string'||!new RegExp(`^[a-f0-9]{${size*2}}$`).test(value))fail('GEO_SAVE_CHECKPOINT_INVALID');return Uint8Array.from(value.match(/../g),part=>parseInt(part,16));};
function serial(record){
 if(!record||!bytes(record.authorKey,64)||!integer(record.authorGeneration,1,0xffffffff)||!bytes(record.owner,16)||!record.owner.some(Boolean)||!bytes(record.id,16)||!record.id.some(Boolean)||!bytes(record.transferId,16)||!record.transferId.some(Boolean))fail('GEO_SAVE_CHECKPOINT_INVALID');
 let expected;try{if(typeof record.expected!=='bigint'&&!Number.isSafeInteger(record.expected))fail('GEO_SAVE_CHECKPOINT_INVALID');expected=BigInt(record.expected);}catch{fail('GEO_SAVE_CHECKPOINT_INVALID');}
 if(expected<0n||expected>=0xffffffffffffffffn)fail('GEO_SAVE_CHECKPOINT_INVALID');
 let fields;try{fields=validateDraftFields(record.fields);encodeDraftEdit(record.id,fields,expected);}catch{fail('GEO_SAVE_CHECKPOINT_INVALID');}
 return JSON.stringify({version:1,authorKey:hex(record.authorKey),authorGeneration:record.authorGeneration,owner:hex(record.owner),id:hex(record.id),transferId:hex(record.transferId),expected:String(expected),fields});
}
export function loadDraftSave(author,{storage=localStorage}={}){
 if(!bytes(author,64))fail('GEO_SAVE_CHECKPOINT_INVALID');let text;try{text=storage.getItem(key(author));}catch{fail('GEO_SAVE_STORAGE');}if(text===null)return null;
 if(typeof text!=='string'||text.length>8192)fail('GEO_SAVE_CHECKPOINT_INVALID');let value;try{value=JSON.parse(text);}catch{fail('GEO_SAVE_CHECKPOINT_INVALID');}
 if(!value||value.version!==1||typeof value.expected!=='string'||!/^(0|[1-9][0-9]*)$/.test(value.expected)||value.expected.length>20)fail('GEO_SAVE_CHECKPOINT_INVALID');
 const version=BigInt(value.expected),expected=version<=BigInt(Number.MAX_SAFE_INTEGER)?Number(version):version;
 const record={authorKey:parse(value.authorKey,64),authorGeneration:value.authorGeneration,owner:parse(value.owner,16),id:parse(value.id,16),transferId:parse(value.transferId,16),expected,fields:value.fields};
 if(!equal(record.authorKey,author)||serial(record)!==text)fail('GEO_SAVE_CHECKPOINT_INVALID');return record;
}
// Persist before the first upload. One bounded unresolved save per author.
export function retainDraftSave(record,{storage=localStorage}={}){
 const text=serial(record);let old;try{old=storage.getItem(key(record.authorKey));}catch{fail('GEO_SAVE_STORAGE');}
 if(old!==null&&old!==text)fail('GEO_SAVE_RETAINED');
 try{storage.setItem(key(record.authorKey),text);}catch{fail('GEO_SAVE_STORAGE');}
 const retained=loadDraftSave(record.authorKey,{storage});if(!retained||serial(retained)!==text)fail('GEO_SAVE_STORAGE');return retained;
}
export function clearDraftSave(record,{storage=localStorage}={}){
 const expected=serial(record);let old;try{old=storage.getItem(key(record.authorKey));}catch{fail('GEO_SAVE_STORAGE');}
 if(old===null)return;if(old!==expected)fail('GEO_SAVE_CHECKPOINT_STALE');
 try{storage.removeItem(key(record.authorKey));if(storage.getItem(key(record.authorKey))!==null)fail('GEO_SAVE_STORAGE');}catch{fail('GEO_SAVE_STORAGE');}
}
export function assertDraftSaveRetained(record,options={}){
 const retained=loadDraftSave(record.authorKey,options);if(!retained||serial(retained)!==serial(record))fail('GEO_SAVE_CHECKPOINT_STALE');return retained;
}
export async function withDraftSaveLock(author,work,{locks=globalThis.navigator?.locks}={}){
 if(!bytes(author,64)||!locks?.request)fail('GEO_SAVE_LOCK_UNAVAILABLE');
 const digest=hex(new Uint8Array(await crypto.subtle.digest('SHA-256',author)));
 return locks.request('trailmate-geo-draft-save-'+digest,{mode:'exclusive',ifAvailable:true},lock=>{if(!lock)fail('GEO_SAVE_BUSY');return work();});
}
// A read result proves this exact edit at expected+1; a newer or different
// snapshot must never be accepted as a receipt for the retained operation.
export function draftSaveSnapshotMatches(record,snapshot,author){
 serial(record);
 if(!author?.verified||!equal(author.publicKey,record.authorKey)||!snapshot?.editable||!equal(snapshot.draft?.id,record.id))return false;
 const draft=snapshot.draft;if(BigInt(draft.generation)!==BigInt(record.expected)+1n)return false;
 const {authorKey,baseHash,id,generation,...fields}=draft;
 return equal(encodeDraftEdit(id,fields,record.expected),encodeDraftEdit(record.id,record.fields,record.expected));
}
