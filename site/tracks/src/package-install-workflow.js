import {equal} from '../../geocaching/src/protocol.js';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {installDeviceTrackPackage} from './device-package-install.js';
import {queryInstalledDevicePackage} from './device-package-query.js';
import {verifyInstallPayload,loadPackageInstallTask,retainPackageInstallTask,updatePackageInstallTask,forgetPackageInstallTask,packageInstallHex} from './package-install-store.js';
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};
const fail=code=>{throw Object.assign(Error(code),{code});};
async function locked(author,work,options){const locks=options.locks??globalThis.navigator?.locks;if(!locks?.request)fail('PACKAGE_STORAGE_UNAVAILABLE');return locks.request('trailmate-package-install:'+packageInstallHex(author.publicKey),{mode:'exclusive',ifAvailable:true},lock=>{if(!lock)fail('PACKAGE_TASK_BUSY');check(options.signal);return work();});}
function createTask(author,checked,previous){
 const id=()=>crypto.getRandomValues(new Uint8Array(16)),hasIndex=Boolean(checked.payload.attachmentIndex);
 return {schema:1,key:packageInstallHex(author.publicKey),actor:author.publicKey.slice(),generation:author.generation,version:(previous?.version??0)+1,stage:'uploading',bytes:checked.bytes,
  title:checked.verified.record[9],originalAuthor:checked.verified.record[1].slice(),signed:checked.payload.signed.map(value=>value.slice()),objectId:checked.verified.objectId.slice(),revisionHash:checked.verified.revisionHash.slice(),operationId:id(),queryId:id(),kmlUploadId:id(),signedUploadId:id(),indexUploadId:hasIndex?id():null,packageUploadId:hasIndex?id():null,
  attachmentUploadIds:checked.payload.attachmentFiles.map(file=>({id:file.id,uploadId:id()})),updatedAt:Date.now()};
}
async function runInstall(session,task,payload,author,options){
 let current=task;
 try{check(options.signal);await installDeviceTrackPackage(session,{...current,...payload,author,attachmentIndex:payload.attachmentIndex??undefined},{...options});check(options.signal);current=await updatePackageInstallTask(current,'installed');options.onCheckpoint?.(current);return current;}
 catch(error){current=await updatePackageInstallTask(current,'unknown');options.onCheckpoint?.(current);throw error;}
}
export async function beginPackageInstallation(session,packet,options={}){
 if(![1,2,32,33,35,100,101].every(op=>options.operations?.includes(op)))fail('PACKAGE_UNSUPPORTED');
 const checked=await verifyInstallPayload({signed:[packet?.detail?.manifest,packet?.detail?.signature],kml:packet?.detail?.source,attachmentIndex:packet?.attachmentIndex,attachmentFiles:packet?.files});check(options.signal);
 const author=await (options.proveAuthor??proveDeviceAuthor)(session,{signal:options.signal});check(options.signal);
 return locked(author,async()=>{
  const old=await loadPackageInstallTask(author.publicKey);if(old&&['uploading','unknown'].includes(old.stage))fail('PACKAGE_TASK_PENDING');
  const task=createTask(author,checked,old);await retainPackageInstallTask(task,checked.payload,old);options.onCheckpoint?.(task);check(options.signal);
  return runInstall(session,task,checked.payload,author,options);
 },options);
}
export async function restorePackageInstallation(session,options={}){const author=await (options.proveAuthor??proveDeviceAuthor)(session,{signal:options.signal});check(options.signal);return loadPackageInstallTask(author.publicKey);}
export async function queryPackageInstallation(session,options={}){
 const author=await (options.proveAuthor??proveDeviceAuthor)(session,{signal:options.signal});check(options.signal);
 return locked(author,async()=>{const task=await loadPackageInstallTask(author.publicKey);if(!task)return null;
  const result=await queryInstalledDevicePackage(session,{author,operationId:task.queryId,objectId:task.objectId,revisionHash:task.revisionHash},options);check(options.signal);
  const next=await updatePackageInstallTask(task,result.installed?'installed':'missing');options.onCheckpoint?.(next);return next;
 },options);
}
export async function resumePackageInstallation(session,options={}){
 const author=await (options.proveAuthor??proveDeviceAuthor)(session,{signal:options.signal});check(options.signal);
 return locked(author,async()=>{const saved=await loadPackageInstallTask(author.publicKey,{payload:true});if(!saved)return null;
  let task=saved.task;
  // Resolve a lost success first; generation changes cannot reuse old uploads.
  const result=await queryInstalledDevicePackage(session,{author,operationId:task.queryId,objectId:task.objectId,revisionHash:task.revisionHash},options);check(options.signal);
  if(result.installed){task=await updatePackageInstallTask(task,'installed');options.onCheckpoint?.(task);return task;}
  task=await updatePackageInstallTask(task,'missing');options.onCheckpoint?.(task);
  if(author.generation!==task.generation)fail('PACKAGE_GENERATION_CHANGED');
  task=await updatePackageInstallTask(task,'uploading');options.onCheckpoint?.(task);
  return runInstall(session,task,saved.payload,author,options);
 },options);
}
export async function forgetPackageInstallation(session,options={}){
 const author=await (options.proveAuthor??proveDeviceAuthor)(session,{signal:options.signal});check(options.signal);
 return locked(author,async()=>{const task=await loadPackageInstallTask(author.publicKey);if(task)await forgetPackageInstallTask(task);return null;},options);
}
export async function restartPackageInstallation(session,options={}){
 if(![1,2,32,33,35,100,101].every(op=>options.operations?.includes(op)))fail('PACKAGE_UNSUPPORTED');
 const author=await (options.proveAuthor??proveDeviceAuthor)(session,{signal:options.signal});check(options.signal);
 return locked(author,async()=>{
  const saved=await loadPackageInstallTask(author.publicKey,{payload:true});if(!saved)return null;
  const result=await queryInstalledDevicePackage(session,{author,operationId:saved.task.queryId,objectId:saved.task.objectId,revisionHash:saved.task.revisionHash},options);check(options.signal);
  const previous=await updatePackageInstallTask(saved.task,result.installed?'installed':'missing');options.onCheckpoint?.(previous);
  if(result.installed)return previous;
  const checked=await verifyInstallPayload(saved.payload);check(options.signal);
  const task=createTask(author,checked,previous);
  await retainPackageInstallTask(task,checked.payload,previous);options.onCheckpoint?.(task);check(options.signal);
  return runInstall(session,task,checked.payload,author,options);
 },options);
}
