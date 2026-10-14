import {queryTrackResultBatch} from './query-result-batch.js';
import {integer} from '../../geocaching/src/protocol.js';
export async function querySharedTrackRegion(client,region,{signal,limit=16,maxPages=8,maxItems=256,continuation=null,scopeId='',...filters}={}){
 if(!integer(limit,1,32))throw Object.assign(Error('TRACK_QUERY_OPTIONS_INVALID'),{code:'TRACK_QUERY_OPTIONS_INVALID'});
 const settings={...filters,author:filters.author instanceof Uint8Array?filters.author.slice():filters.author??null,tags:Array.isArray(filters.tags)?filters.tags.slice():filters.tags??[]};
 const context=['trailmate.track.region.continuation.v1',scopeId,region.id,region.version,region.bounds,settings.kind??null,settings.author,settings.state===undefined?0:settings.state,settings.activity??null,settings.tags];
 return queryTrackResultBatch((part,options)=>client.query(region,{...settings,limit,...options}),context,{maxPages,maxItems,continuation,signal});
}
