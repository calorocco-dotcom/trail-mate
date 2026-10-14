import {bytes,equal,encode,decode} from '../../geocaching/src/protocol.js';
import {trackDirectoryError} from './directory-error.js';
import {TRACK_BULK_OPERATION,TRACK_BULK_CHUNK_BYTES} from './bulk-file-profile.js';

export async function createIPTrackReadSession({network,directory,assertCurrent}={}){
 if(!network||typeof network.enqueue!=='function'||typeof network.request!=='function'||typeof assertCurrent!=='function'||
    !directory?.capabilitiesVerified||!bytes(directory.publicKey,64)||!bytes(directory.delivery,16)||!bytes(directory.epoch,16))throw Error('Invalid track IP read context');
 const entry=network.directories.get(directory.key),epoch=directory.epoch.slice(),key=directory.publicKey.slice(),delivery=directory.delivery.slice(),generation=network.generation;
 if(!entry?.ready||!equal(entry.epoch,epoch)||!equal(entry.destination,delivery)||!equal(await entry.identity.getPublicKey(),key))throw Error('Track IP directory binding changed');
 let closed=false;const bulkFiles=entry.operations?.includes(TRACK_BULK_OPERATION)===true;
 const check=signal=>{assertCurrent();if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');
  if(closed||network.closed||network.generation!==generation||network.directories.get(directory.key)!==entry||!entry.ready||!equal(entry.epoch,epoch))throw Error('Track IP read context changed');};
 check();
 return {capabilities:{maxFileBytes:8388608,chunkBytes:bulkFiles?TRACK_BULK_CHUNK_BYTES:1024,pageLimit:16,packageSupported:entry.operations?.includes(14)===true,metadataSupported:entry.operations?.includes(15)===true,metadataRegionSupported:entry.operations?.includes(16)===true,conflictSupported:entry.operations?.includes(18)===true},
  request(operation,body,{signal}={}){
   check(signal);if(![0,1,2,8,9,10,11,12,13,14,15,16,18].includes(operation))throw Error('Unsupported track read operation');
   if(operation===18&&!entry.operations?.includes(18))throw Object.assign(Error('Directory conflict proof unavailable'),{code:'TRACK_CONFLICT_UNSUPPORTED'});
   if([15,16].includes(operation)&&!entry.operations?.includes(operation))throw Object.assign(Error('Directory metadata query unavailable'),{code:'TRACK_QUERY_METADATA_UNSUPPORTED'});
   if(operation===14&&!entry.operations?.includes(14))throw Object.assign(Error('Directory package support unavailable'),{code:'TRACK_PACKAGE_UNSUPPORTED'});
   const copied=decode(encode(body),8192);
   return network.enqueue(async()=>{check(signal);let response;
    try{response=await network.request(entry,operation===8&&bulkFiles?TRACK_BULK_OPERATION:operation,operation===8&&bulkFiles?[2,...copied]:copied);}catch(error){if(error.status===422&&error.reason==='track author version conflict')throw Object.assign(Error(error.message),{code:'TRACK_AUTHOR_VERSION_CONFLICT',status:422,diagnostic:error.reason});if(error.status===422&&error.reason==='region capability unsupported')throw Object.assign(Error(error.message),{status:422,diagnostic:error.reason});throw trackDirectoryError(error);}
    check(signal);return response;
   });
  },close(){closed=true;}};
}
