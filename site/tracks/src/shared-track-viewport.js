import {sharedTrackViewportBoxes,querySharedTrackPage} from './shared-track-query.js';
import {queryTrackResultBatch} from './query-result-batch.js';
import {integer} from '../../geocaching/src/protocol.js';
// Date-line boxes retain one directory snapshot. Each box keeps its cursor;
// opaque continuations preserve pending page items and verified duplicate hashes.
export async function querySharedTrackViewport(request,bounds,{signal,limit=16,maxPages=8,maxItems=256,continuation=null,scopeId='',...filters}={}){
 if(!integer(limit,1,32))throw Object.assign(Error('TRACK_QUERY_OPTIONS_INVALID'),{code:'TRACK_QUERY_OPTIONS_INVALID'});
 const boxes=sharedTrackViewportBoxes(bounds),settings={...filters,author:filters.author instanceof Uint8Array?filters.author.slice():filters.author??null,tags:Array.isArray(filters.tags)?filters.tags.slice():filters.tags??[]};
 const context=['trailmate.track.viewport.continuation.v1',scopeId,boxes,settings.kind??null,settings.author,settings.state===undefined?0:settings.state,settings.activity??null,settings.tags];
 return queryTrackResultBatch((part,options)=>querySharedTrackPage(request,{...settings,box:boxes[part],limit,...options}),context,{parts:boxes.length,maxPages,maxItems,continuation,signal});
}
