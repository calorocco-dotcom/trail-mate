import {Identity} from '@reticulum/core';
import {bytes,equal,encode,integer} from '../../geocaching/src/protocol.js';
import {validateSharedTrackSummary} from './shared-track-query.js';
const tokens=new WeakMap(),hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const fail=code=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};
const wire=row=>[row.objectId,row.revisionHash,row.revision,row.kind,row.state,row.title,row.bounds,row.statistics,row.fileBytes,row.author,...(row.activity===undefined?[]:[row.activity,row.tags])];
const copyDirectory=value=>Object.freeze({key:value.key,name:value.name??value.key,capabilitiesVerified:true,publicKey:value.publicKey.slice(),delivery:value.delivery.slice(),epoch:value.epoch.slice()});
const copyState=state=>({...state,sources:state.sources.map(value=>({...value,pending:value.pending.slice()})),seen:new Map([...state.seen].map(([key,value])=>[key,{...value,providers:new Set(value.providers)}])),window:new Set(state.window)});
const nonRetryable=code=>/MISMATCH|CHANGED|INVALID|UNSUPPORTED/.test(code??'');

// Each source owns its snapshot/cursor. This combines directory assertions,
// never chooses a current winner and never authenticates an author from a list.
// readSource must return an already validated batch of at most 16 summaries.
export async function queryTrackDirectories(readSource,directories,context,{continuation=null,retryFailed=false,signal,maxRequests=8,maxItems=256,maxSeen=8192,onProgress}={}){
 check(signal);
 if(typeof readSource!=='function'||!Array.isArray(directories)||!integer(directories.length,1,32)||!integer(maxRequests,1,32)||!integer(maxItems,1,512)||!integer(maxSeen,maxItems,8192))fail('TRACK_MULTIDIRECTORY_OPTIONS_INVALID');
 const keys=new Set(),destinations=new Set(),sources=directories.map(value=>{
  if(!value?.capabilitiesVerified||typeof value.key!=='string'||!value.key||keys.has(value.key)||!bytes(value.publicKey,64)||!bytes(value.delivery,16)||!bytes(value.epoch,16)||destinations.has(hex(value.delivery)))fail('TRACK_MULTIDIRECTORY_OPTIONS_INVALID');
  keys.add(value.key);destinations.add(hex(value.delivery));return copyDirectory(value);
 }).sort((a,b)=>hex(a.delivery).localeCompare(hex(b.delivery)));
 const binding=await Identity.fullHash(encode(['trailmate.track.multidirectory.v1',sources.map(value=>[value.key,value.publicKey,value.delivery,value.epoch]),context,maxItems,maxSeen]));check(signal);
 let state={binding,sources:sources.map(directory=>({directory,continuation:null,pending:[],started:false,done:false,error:null,retryable:false})),seen:new Map(),window:new Set(),next:0,resetWindow:false,capacity:false};
 if(continuation!==null){const previous=tokens.get(continuation);if(!previous||!equal(previous.binding,binding))fail('TRACK_QUERY_CURSOR_MISMATCH');state=copyState(previous);}
 if(state.resetWindow){state.window.clear();state.resetWindow=false;}
 if(retryFailed)for(const source of state.sources)if(source.error&&source.retryable){source.error=null;source.retryable=false;}
 const publish=()=>{
  const groups=new Map();
  for(const key of state.window){const item=state.seen.get(key),id=hex(item.row.objectId);let group=groups.get(id);if(!group){group={...structuredClone(item.row),authorVerified:false,variants:[]};groups.set(id,group);}group.variants.push({...structuredClone(item.row),authorVerified:false,sources:[...item.providers].map(key=>copyDirectory(sources.find(value=>value.key===key)))});}
  const items=[...groups.values()].sort((a,b)=>hex(a.objectId).localeCompare(hex(b.objectId)));
  for(const item of items){item.variants.sort((a,b)=>a.revision-b.revision||hex(a.revisionHash).localeCompare(hex(b.revisionHash)));item.multipleVersions=new Set(item.variants.map(value=>hex(value.revisionHash))).size>1;item.summaryDisagreement=item.variants.length>new Set(item.variants.map(value=>hex(value.revisionHash))).size;}
  const pending=state.sources.some(value=>!value.error&&(value.pending.length||!value.done)),failed=state.sources.some(value=>value.error),retryable=state.sources.some(value=>value.error&&value.retryable);
  const snapshot=Object.freeze({type:'track-multidirectory-continuation'});tokens.set(snapshot,copyState(state));
  return {items,aggregate:true,sources:state.sources.map(value=>({directory:copyDirectory(value.directory),status:value.error?'failed':value.done&&!value.pending.length?'complete':value.started?'more':'pending',error:value.error,retryable:value.retryable})),continuation:pending&&!state.capacity?snapshot:null,retry:retryable&&!state.capacity?snapshot:null,truncated:pending||failed||state.capacity,partial:failed,capacityReached:state.capacity,windowFull:state.resetWindow};
 };
 let requests=0,visits=0;
 while(visits<state.sources.length&&requests<maxRequests&&!state.capacity){
  check(signal);const index=state.next%state.sources.length;state.next=(index+1)%state.sources.length;visits++;const source=state.sources[index];
  if(source.error||source.done&&!source.pending.length)continue;
  if(!source.pending.length){
   try{
    requests++;const result=await readSource(copyDirectory(source.directory),{continuation:source.continuation,signal});check(signal);
    if(!result||!Array.isArray(result.items)||result.items.length>16||result.continuation!==null&&typeof result.continuation!=='object')fail('TRACK_MULTIDIRECTORY_RESPONSE_INVALID');
    source.pending=result.items.map(row=>validateSharedTrackSummary(wire(structuredClone(row)),{metadata:row.activity!==undefined}));source.continuation=result.continuation;source.started=true;source.done=result.continuation===null;
   }catch(error){check(signal);source.error=error.code??'TRACK_DIRECTORY_UNAVAILABLE';source.retryable=!nonRetryable(source.error);onProgress?.(publish());continue;}
  }
  while(source.pending.length){
   check(signal);const row=source.pending[0],digest=hex(await Identity.fullHash(encode(wire(row))));check(signal);const key=hex(row.objectId)+':'+hex(row.revisionHash)+':'+digest,old=state.seen.get(key);
   if(old){old.providers.add(source.directory.key);source.pending.shift();continue;}
   if(state.window.size>=maxItems){state.resetWindow=true;break;}
   if(state.seen.size>=maxSeen){state.capacity=true;break;}
   state.seen.set(key,{row,providers:new Set([source.directory.key])});state.window.add(key);source.pending.shift();
  }
  onProgress?.(publish());if(state.resetWindow)break;
 }
 check(signal);return publish();
}
