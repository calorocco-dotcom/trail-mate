import {bytes,integer} from '../../geocaching/src/protocol.js';
const prefix='trailmate.route-install.v1.';
const stages=new Set(['uploading','submitting','admitted','unknown','installed','missing','rejected']);
const unresolved=new Set(['uploading','submitting','admitted','unknown']);
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const fail=code=>{throw Object.assign(Error(code),{code});};
function key(author){if(!bytes(author,64))fail('ROUTE_AUTHOR_INVALID');return prefix+hex(author);}
function encode(record){
 if(!bytes(record?.id,16)||!record.id.some(byte=>byte)||!bytes(record.sha256,32)||!bytes(record.authorKey,64)||!integer(record.bytes,1,8388608)||!integer(record.generation,1,0xfffffffe)||!stages.has(record.stage))fail('ROUTE_CHECKPOINT_INVALID');
 return {schema:1,id:hex(record.id),sha256:hex(record.sha256),authorKey:hex(record.authorKey),bytes:record.bytes,generation:record.generation,stage:record.stage};
}
function decode(value,author){
 if(!value||value.schema!==1)fail('ROUTE_CHECKPOINT_INVALID');
 const parse=(name,size)=>{const raw=value[name];if(typeof raw!=='string'||!new RegExp(`^[0-9a-f]{${size*2}}$`).test(raw))fail('ROUTE_CHECKPOINT_INVALID');return Uint8Array.from(raw.match(/../g),pair=>parseInt(pair,16));};
 const record={id:parse('id',16),sha256:parse('sha256',32),authorKey:parse('authorKey',64),bytes:value.bytes,generation:value.generation,stage:value.stage};
 encode(record);if(hex(record.authorKey)!==hex(author))fail('ROUTE_CHECKPOINT_INVALID');return record;
}
export function loadRouteInstallCheckpoint(author,{storage=globalThis.localStorage}={}){
 const name=key(author);let raw;
 try{raw=storage.getItem(name);}catch{fail('ROUTE_CHECKPOINT_STORAGE');}
 if(raw===null)return null;
 if(typeof raw!=='string'||raw.length>1024)fail('ROUTE_CHECKPOINT_INVALID');
 let parsed;try{parsed=JSON.parse(raw);}catch{fail('ROUTE_CHECKPOINT_INVALID');}
 return decode(parsed,author);
}
export function saveRouteInstallCheckpoint(record,{storage=globalThis.localStorage}={}){
 const encoded=encode(record),existing=loadRouteInstallCheckpoint(record.authorKey,{storage});
 if(existing&&unresolved.has(existing.stage)&&hex(existing.id)!==encoded.id)fail('ROUTE_INSTALL_PENDING');
 const raw=JSON.stringify(encoded),name=key(record.authorKey);
 try{storage.setItem(name,raw);if(storage.getItem(name)!==raw)fail('ROUTE_CHECKPOINT_STORAGE');}catch{fail('ROUTE_CHECKPOINT_STORAGE');}
 return decode(encoded,record.authorKey);
}
export function forgetRouteInstallCheckpoint(author,{storage=globalThis.localStorage,expected}={}){
 if(expected){const current=loadRouteInstallCheckpoint(author,{storage});if(!current||JSON.stringify(encode(current))!==JSON.stringify(encode(expected)))fail('ROUTE_CHECKPOINT_STALE');}
 const name=key(author);try{storage.removeItem(name);if(storage.getItem(name)!==null)fail('ROUTE_CHECKPOINT_STORAGE');}catch{fail('ROUTE_CHECKPOINT_STORAGE');}
}
export function routeInstallUnresolved(record){return Boolean(record&&unresolved.has(record.stage));}
