import {Identity} from '@reticulum/core';
import {equal} from '../../geocaching/src/protocol.js';
const DATABASE='trailmate-track-transport',STORE='identities',CAPACITY=32;
const failure=code=>Object.assign(Error('Browser track network session unavailable'),{code});
export function normalizeTrackTransportEndpoint(value){
 try{const endpoint=new URL(value);if(endpoint.username||endpoint.password||endpoint.hash||new TextEncoder().encode(endpoint.href).length>4096||!(endpoint.protocol==='wss:'||endpoint.protocol==='ws:'&&['localhost','127.0.0.1','[::1]'].includes(endpoint.hostname)))throw Error();return endpoint.href;}
 catch{throw failure('TRACK_TRANSPORT_ENDPOINT_INVALID');}
}
function open(){return new Promise((resolve,reject)=>{
 if(!globalThis.indexedDB){reject(failure('TRACK_TRANSPORT_STORAGE'));return;}
 const request=indexedDB.open(DATABASE,1);let blocked=false;
 request.onupgradeneeded=()=>request.result.createObjectStore(STORE,{keyPath:'endpoint'});
 request.onblocked=()=>{blocked=true;reject(failure('TRACK_TRANSPORT_STORAGE'));};
 request.onerror=()=>reject(failure('TRACK_TRANSPORT_STORAGE'));
 request.onsuccess=()=>{const db=request.result;if(blocked){db.close();return;}db.onversionchange=()=>db.close();resolve(db);};
});}
function read(db,endpoint){return new Promise((resolve,reject)=>{const tx=db.transaction(STORE),request=tx.objectStore(STORE).get(endpoint);tx.oncomplete=()=>resolve(request.result);tx.onabort=()=>reject(failure('TRACK_TRANSPORT_STORAGE'));tx.onerror=()=>{};});}
async function retainedKey(row,endpoint){
 if(!row||Object.keys(row).sort().join(',')!=='endpoint,privateKey,publicKey,version'||row.endpoint!==endpoint||row.version!==1||!(row.privateKey instanceof ArrayBuffer)||row.privateKey.byteLength!==64||!(row.publicKey instanceof ArrayBuffer)||row.publicKey.byteLength!==64)throw failure('TRACK_TRANSPORT_CORRUPT');
 const key=new Uint8Array(row.privateKey);
 try{const identity=await Identity.fromPrivateKey(key);if(!identity||!equal(await identity.getPublicKey(),new Uint8Array(row.publicKey)))throw Error();return key.slice();}
 catch{throw failure('TRACK_TRANSPORT_CORRUPT');}
 finally{key.fill(0);}
}
async function createRow(endpoint){
 const identity=await Identity.generate(),exported=await identity.getPrivateKey(),key=new Uint8Array(64);
 try{
  // Installed core exports X private/public then Ed private/public (128 bytes),
  // while fromPrivateKey accepts only the two private seeds (64 bytes).
  if(!(exported instanceof Uint8Array)||exported.length!==128)throw Error();
  key.set(exported.subarray(0,32));key.set(exported.subarray(64,96),32);
  const publicKey=await identity.getPublicKey(),restored=await Identity.fromPrivateKey(key);
  if(!restored||!equal(await restored.getPublicKey(),publicKey))throw Error();
  return {endpoint,version:1,privateKey:key.slice().buffer,publicKey:publicKey.slice().buffer};
 }catch{throw failure('TRACK_TRANSPORT_STORAGE');}
 finally{exported.fill(0);key.fill(0);}
}
function add(db,row){return new Promise((resolve,reject)=>{
 const tx=db.transaction(STORE,'readwrite'),store=tx.objectStore(STORE),existing=store.get(row.endpoint);let problem;
 tx.oncomplete=()=>resolve(existing.result??row);tx.onabort=()=>reject(problem??failure('TRACK_TRANSPORT_STORAGE'));tx.onerror=()=>{};
 existing.onsuccess=()=>{if(existing.result)return;const count=store.count();count.onsuccess=()=>{try{if(count.result>=CAPACITY)throw failure('TRACK_TRANSPORT_FULL');store.add(row);}catch(error){problem=typeof error.code==='string'&&error.code.startsWith('TRACK_TRANSPORT_')?error:failure('TRACK_TRANSPORT_STORAGE');tx.abort();}};};
});}
// IndexedDB's read/write transaction serializes creation across tabs. A racing
// creator reads the first committed record and never overwrites its identity.
export async function readTrackTransportKey(value){
 const endpoint=normalizeTrackTransportEndpoint(value),db=await open();let row;
 try{row=await read(db,endpoint);if(!row){const candidate=await createRow(endpoint);try{await add(db,candidate);row=await read(db,endpoint);}finally{new Uint8Array(candidate.privateKey).fill(0);}}return await retainedKey(row,endpoint);}
 catch(error){if(typeof error.code==='string'&&error.code.startsWith('TRACK_TRANSPORT_'))throw error;throw failure('TRACK_TRANSPORT_STORAGE');}
 finally{db.close();}
}
