import {Identity} from '@reticulum/core';
import {encode,equal,integer} from '../../geocaching/src/protocol.js';
const continuations=new WeakMap(),hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
const fail=code=>{throw Object.assign(Error(code),{code});};
const raw=item=>[item.objectId,item.revisionHash,item.revision,item.kind,item.state,item.title,item.bounds,item.statistics,item.fileBytes,item.author,...(item.activity===undefined?[]:[item.activity,item.tags])];
// Opaque, in-memory continuations belong to this exact context. Pending items
// retain the remainder of a fetched page, so a UI budget never skips results.
export async function queryTrackResultBatch(readPage,context,{parts=1,maxPages=8,maxItems=256,continuation=null,signal}={}){
 check(signal);
 if(typeof readPage!=='function'||!integer(parts,1,4)||!integer(maxPages,1,64)||!integer(maxItems,1,2048))fail('TRACK_QUERY_OPTIONS_INVALID');
 const binding=await Identity.fullHash(encode(['trailmate.track.batch.v1',parts,context]));check(signal);
 let state={binding,snapshot:null,part:0,cursor:null,pending:[],seen:new Map()};
 if(continuation!==null){const previous=continuations.get(continuation);
  if(!previous||!equal(previous.binding,binding))fail('TRACK_QUERY_CURSOR_MISMATCH');
  state={...previous,pending:previous.pending.slice(),cursor:previous.cursor?.map(value=>value.slice())??null,seen:new Map(previous.seen)};
 }
 const items=new Map(),digests=new Map();let pages=0;
 for(;;){
  check(signal);
  if(state.pending.length){
   const item=state.pending[0],id=hex(item.objectId),digest=await Identity.fullHash(encode(raw(item)));check(signal);
   const existing=state.seen.get(id)??digests.get(id);
   if(existing){if(!equal(existing,digest))fail('TRACK_QUERY_OBJECT_CONFLICT');state.pending.shift();continue;}
   if(items.size>=maxItems)break;
   state.pending.shift();items.set(id,item);digests.set(id,digest);if(parts>1)state.seen.set(id,digest);continue;
  }
  if(items.size>=maxItems||state.part>=parts||pages>=maxPages)break;
  const page=await readPage(state.part,{snapshot:state.snapshot,cursor:state.cursor,signal});check(signal);
  state.snapshot=page.snapshot;state.pending=page.items.slice();state.cursor=page.cursor?.map(value=>value.slice())??null;pages++;
  if(state.cursor===null)state.part++;
 }
 let next=null;
 if(state.pending.length||state.part<parts){next=Object.freeze({type:'track-query-continuation'});continuations.set(next,state);}
 return {items:[...items.values()].sort((a,b)=>hex(a.objectId).localeCompare(hex(b.objectId))),snapshot:state.snapshot,truncated:next!==null,pages,continuation:next};
}
