import {OPERATION} from '../../device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {equal} from '../../geocaching/src/protocol.js';
import {parseKml,KML_LIMIT} from './kml.js';
import {uploadTrackFile} from './device-upload.js';
import {installUploadedRoute,queryRouteInstallation} from './device-route-install.js';
import {loadRouteInstallCheckpoint,saveRouteInstallCheckpoint,routeInstallUnresolved,forgetRouteInstallCheckpoint} from './route-install-journal.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
export async function forgetRouteInstallation(record,{storage=globalThis.localStorage,locks=globalThis.navigator?.locks,signal}={}){
 if(!locks?.request)fail('ROUTE_CHECKPOINT_STORAGE');
 const fingerprint=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',record.authorKey)),byte=>byte.toString(16).padStart(2,'0')).join('');check(signal);
 return locks.request('trailmate-route-install:'+fingerprint,{mode:'exclusive',ifAvailable:true},async lock=>{
  if(!lock)fail('ROUTE_INSTALL_PENDING');check(signal);
  forgetRouteInstallCheckpoint(record.authorKey,{storage,expected:record});return null;
 });
}
export async function beginRouteInstallation(session,source,{operations,storage=globalThis.localStorage,locks=globalThis.navigator?.locks,signal,onCheckpoint=()=>{},onProgress=()=>{}}={}){
 if(![1,2,32,33,34,35,85,95].every(op=>operations?.includes(op)))fail('ROUTE_UNSUPPORTED');
 if(!(source instanceof Uint8Array)||!source.length||source.length>KML_LIMIT.bytes)fail('ROUTE_KML_INVALID');
 const content=source.slice(),model=parseKml(content);if(!model.pointCount)fail('ROUTE_KML_INVALID');check(signal);
 const author=await proveDeviceAuthor(session,{signal});check(signal);
 if(!locks?.request)fail('ROUTE_CHECKPOINT_STORAGE');
 return locks.request('trailmate-route-install:'+author.fingerprint,{mode:'exclusive',ifAvailable:true},async lock=>{
  if(!lock)fail('ROUTE_INSTALL_PENDING');
  const existing=loadRouteInstallCheckpoint(author.publicKey,{storage});if(routeInstallUnresolved(existing))fail('ROUTE_INSTALL_PENDING');
  const sha256=new Uint8Array(await crypto.subtle.digest('SHA-256',content));check(signal);
  const id=crypto.getRandomValues(new Uint8Array(16));
  let record={id,sha256,bytes:content.length,authorKey:author.publicKey.slice(),generation:author.generation,stage:'uploading'};
  const persist=next=>{record=saveRouteInstallCheckpoint(next,{storage});onCheckpoint(record);return record;};
  // Durable browser binding precedes the very first upload/installation write.
  persist(record);
  try{
   const uploaded=await uploadTrackFile(session,content,{id,signal,onProgress,digest:async raw=>{
    const actual=new Uint8Array(await crypto.subtle.digest('SHA-256',raw));if(!equal(actual,sha256))fail('ROUTE_KML_CHANGED');return actual;
   }});
   check(signal);
   const current=await proveDeviceAuthor(session,{signal});check(signal);
   if(current.generation!==author.generation||!equal(current.publicKey,author.publicKey))fail('ROUTE_AUTHOR_CHANGED');
   persist({...record,stage:'submitting'});
   const result=await installUploadedRoute(session,uploaded,current,{signal});
   return persist({...record,stage:result.installed?'installed':'admitted'});
  }catch(error){
   if(record.stage==='submitting'){
    const rejected=Number.isInteger(error.code)&&error.code>=1&&error.code<=10;
    persist({...record,stage:rejected?'rejected':'unknown'});
   }
   throw error;
  }
 });
}
export async function restoreRouteInstallation(session,{storage=globalThis.localStorage,signal}={}){
 const author=await proveDeviceAuthor(session,{signal});check(signal);
 return loadRouteInstallCheckpoint(author.publicKey,{storage});
}
export async function confirmRouteInstallation(session,record,{storage=globalThis.localStorage,locks=globalThis.navigator?.locks,signal,onCheckpoint=()=>{}}={}){
 const author=await proveDeviceAuthor(session,{signal});check(signal);
 if(!equal(author.publicKey,record.authorKey))fail('ROUTE_AUTHOR_CHANGED');
 if(!locks?.request)fail('ROUTE_CHECKPOINT_STORAGE');
 return locks.request('trailmate-route-install:'+author.fingerprint,{mode:'exclusive',ifAvailable:true},async lock=>{
  if(!lock)fail('ROUTE_INSTALL_PENDING');check(signal);
  const current=()=>{
   const saved=loadRouteInstallCheckpoint(author.publicKey,{storage});
   if(!saved||!equal(saved.id,record.id)||!equal(saved.sha256,record.sha256)||saved.bytes!==record.bytes||saved.generation!==record.generation||saved.stage!==record.stage)fail('ROUTE_CHECKPOINT_STALE');
  };
  current();
  const result=await queryRouteInstallation(session,record,author,{signal});check(signal);current();
  const next=saveRouteInstallCheckpoint({...record,generation:author.generation,stage:result.installed?'installed':result.missing?'missing':'admitted'},{storage});
  onCheckpoint(next);return next;
 });
}
