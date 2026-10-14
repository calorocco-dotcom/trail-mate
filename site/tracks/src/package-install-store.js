import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {verifyTrackManifest} from './manifest.js';
import {freezeTrackAttachmentInputs} from './attachment-inputs.js';
import {matchTrackAttachmentIndex} from './attachment-index.js';
export const packageInstallHex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const fail=code=>{throw Object.assign(Error(code),{code});};
const stages=new Set(['uploading','unknown','installed','missing']);
const digest=async source=>new Uint8Array(await crypto.subtle.digest('SHA-256',source));
function open(){return new Promise((resolve,reject)=>{const request=indexedDB.open('trailmate-package-installs',1);request.onupgradeneeded=()=>{for(const name of ['tasks','payloads'])request.result.createObjectStore(name,{keyPath:'key'});};request.onerror=()=>reject(request.error);request.onblocked=()=>reject(Error('Package storage blocked'));request.onsuccess=()=>{request.result.onversionchange=()=>request.result.close();resolve(request.result);};});}
const get=request=>new Promise((resolve,reject)=>{request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);});
export async function verifyInstallPayload(payload){
 if(!payload||!Array.isArray(payload.signed)||payload.signed.length!==2||!(payload.signed[0] instanceof Uint8Array)||payload.signed[0].length>4096||!bytes(payload.signed[1],64)||!(payload.kml instanceof Uint8Array)||!payload.kml.length||payload.kml.length>8388608)fail('PACKAGE_CHECKPOINT_INVALID');
 const signed=payload.signed.map(value=>value.slice()),kml=payload.kml.slice(),frozen=freezeTrackAttachmentInputs(payload.attachmentIndex??undefined,payload.attachmentFiles??[],kml.length);
 const verified=await verifyTrackManifest(signed);
 if(kml.length!==verified.record[13][1]||!equal(await digest(kml),verified.record[13][0]))fail('PACKAGE_CHECKPOINT_INVALID');
 let index;
 if(verified.record[0]===2&&verified.record[15]){
  if(!frozen.index)fail('PACKAGE_REQUIRED_MISSING');
  ({record:index}=await matchTrackAttachmentIndex(frozen.index,signed[0]));
  const seen=new Set();
  for(const file of frozen.files){const row=index[3].find(row=>row[0]===file.id);if(seen.has(file.id)||!row||row[2]!==file.content.length||!equal(await digest(file.content),row[1]))fail('PACKAGE_CHECKPOINT_INVALID');seen.add(file.id);}
  if(index[3].some(row=>row[6]&&!seen.has(row[0])))fail('PACKAGE_REQUIRED_MISSING');
 }else if(frozen.index||frozen.files.length)fail('PACKAGE_CHECKPOINT_INVALID');
 return {payload:{signed,kml,attachmentIndex:frozen.index??null,attachmentFiles:frozen.files},verified,index,
  bytes:kml.length+(frozen.index?.length??0)+frozen.files.reduce((sum,file)=>sum+file.content.length,0)};
}
export async function verifyInstallTask(task,author){
 if(!task||task.schema!==1||!bytes(task.actor,64)||!bytes(author,64)||!equal(task.actor,author)||task.key!==packageInstallHex(author)||!integer(task.version,1,Number.MAX_SAFE_INTEGER)||!integer(task.generation,1,0xffffffff)||!stages.has(task.stage)||!integer(task.bytes,1,67108864)||!Array.isArray(task.attachmentUploadIds)||task.attachmentUploadIds.length>64)fail('PACKAGE_CHECKPOINT_INVALID');
 const all=[task.operationId,task.queryId,task.kmlUploadId,task.signedUploadId];
 if(task.indexUploadId!==null||task.packageUploadId!==null)all.push(task.indexUploadId,task.packageUploadId);
 const names=new Set();
 for(const row of task.attachmentUploadIds){if(!row||typeof row.id!=='string'||!/^[A-Za-z0-9_-]{1,48}$/.test(row.id)||names.has(row.id.toLowerCase()))fail('PACKAGE_CHECKPOINT_INVALID');names.add(row.id.toLowerCase());all.push(row.uploadId);}
 const ids=new Set();for(const id of all){if(!bytes(id,16)||!id.some(Boolean)||ids.has(packageInstallHex(id)))fail('PACKAGE_CHECKPOINT_INVALID');ids.add(packageInstallHex(id));}
 if(!Array.isArray(task.signed)||task.signed.length!==2||!(task.signed[0] instanceof Uint8Array)||task.signed[0].length>4096||!bytes(task.signed[1],64))fail('PACKAGE_CHECKPOINT_INVALID');
 const proof=await verifyTrackManifest(task.signed);
 if(!bytes(task.objectId,32)||!bytes(task.revisionHash,32)||!equal(proof.objectId,task.objectId)||!equal(proof.revisionHash,task.revisionHash))fail('PACKAGE_CHECKPOINT_INVALID');
 const hasIndex=proof.record[0]===2&&proof.record[15]!==null;
 if(hasIndex!==(task.indexUploadId!==null)||hasIndex!==(task.packageUploadId!==null)||!hasIndex&&task.attachmentUploadIds.length)fail('PACKAGE_CHECKPOINT_INVALID');
 return {...task,title:proof.record[9],originalAuthor:proof.record[1].slice(),target:'/routes/p-'+packageInstallHex(task.revisionHash)+'.kml'};
}
export async function loadPackageInstallTask(author,{payload=false}={}){
 const db=await open();try{const tx=db.transaction(payload?['tasks','payloads']:['tasks']),key=packageInstallHex(author),[row,data]=await Promise.all([get(tx.objectStore('tasks').get(key)),payload?get(tx.objectStore('payloads').get(key)):Promise.resolve(null)]);if(!row)return null;
  const task=await verifyInstallTask(row,author);
  if(!payload)return task;
  const checked=await verifyInstallPayload(data?.payload);
  if(checked.bytes!==task.bytes||!equal(checked.verified.objectId,task.objectId)||!equal(checked.verified.revisionHash,task.revisionHash)||checked.payload.attachmentFiles.length!==task.attachmentUploadIds.length||checked.payload.attachmentFiles.some(file=>!task.attachmentUploadIds.some(row=>row.id===file.id)))fail('PACKAGE_CHECKPOINT_INVALID');
  return {task,payload:checked.payload};
 }finally{db.close();}
}
export async function retainPackageInstallTask(task,payload,expected=null){
 await verifyInstallTask(task,task.actor);const checked=await verifyInstallPayload(payload);
 if(checked.bytes!==task.bytes||!equal(checked.verified.revisionHash,task.revisionHash))fail('PACKAGE_CHECKPOINT_INVALID');
 const db=await open();try{return await new Promise((resolve,reject)=>{const tx=db.transaction(['tasks','payloads'],'readwrite'),store=tx.objectStore('tasks'),request=store.getAll();let error;
  tx.oncomplete=()=>resolve(task);tx.onabort=()=>reject(error??tx.error??Error('Package checkpoint save failed'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{const all=request.result,old=all.find(row=>row.key===task.key);
   if(expected?(!old||old.version!==expected.version||!equal(old.operationId,expected.operationId)):old&&['uploading','unknown'].includes(old.stage))fail('PACKAGE_TASK_PENDING');
   if((!old&&all.length>=8)||all.filter(row=>row.key!==task.key).reduce((sum,row)=>sum+row.bytes,0)+task.bytes>134217728)fail('PACKAGE_STORAGE_FULL');
   store.put(task);tx.objectStore('payloads').put({key:task.key,payload:checked.payload});
  }catch(failure){error=failure;tx.abort();}};
 });}finally{db.close();}
}
export async function updatePackageInstallTask(task,stage){
 if(!stages.has(stage))fail('PACKAGE_CHECKPOINT_INVALID');
 const next={...task,version:task.version+1,stage,updatedAt:Date.now()},db=await open();
 try{return await new Promise((resolve,reject)=>{const tx=db.transaction('tasks','readwrite'),store=tx.objectStore('tasks'),request=store.get(task.key);let error;
  tx.oncomplete=()=>resolve(next);tx.onabort=()=>reject(error??tx.error??Error('Package checkpoint update failed'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{const current=request.result;if(!current||current.version!==task.version||!equal(current.operationId,task.operationId))fail('PACKAGE_CHECKPOINT_STALE');store.put(next);}catch(failure){error=failure;tx.abort();}};
 });}finally{db.close();}
}
export async function forgetPackageInstallTask(task){
 const db=await open();try{return await new Promise((resolve,reject)=>{const tx=db.transaction(['tasks','payloads'],'readwrite'),store=tx.objectStore('tasks'),request=store.get(task.key);let error;
  tx.oncomplete=()=>resolve();tx.onabort=()=>reject(error??tx.error??Error('Package checkpoint removal failed'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{const row=request.result;if(!row||row.version!==task.version||!equal(row.operationId,task.operationId))fail('PACKAGE_CHECKPOINT_STALE');store.delete(task.key);tx.objectStore('payloads').delete(task.key);}catch(failure){error=failure;tx.abort();}};
 });}finally{db.close();}
}
