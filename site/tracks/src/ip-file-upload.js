import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {TRACK_BULK_OPERATION,TRACK_BULK_CHUNK_BYTES} from './bulk-file-profile.js';
// The same bounded immutable-byte protocol serves KML, index and resources.
// Signature/package authorization belongs to the publication coordinator.
export async function uploadIPTrackFile(request,source,digest,{signal,onProgress,bulkFiles=false}={}){
 if(typeof request!=='function'||!(source instanceof Uint8Array)||!source.length||source.length>8388608||!bytes(digest,32))throw Error('Invalid track file upload');
 const file=source.slice(),hash=digest.slice(),total=file.length;
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};check();
 if(!equal(new Uint8Array(await crypto.subtle.digest('SHA-256',file)),hash))throw Error('Track file differs from signed manifest');check();
 if(typeof bulkFiles!=='boolean')throw Error('Invalid transfer profile');const chunkBytes=bulkFiles?TRACK_BULK_CHUNK_BYTES:1024;
 const begin=await request(bulkFiles?TRACK_BULK_OPERATION:4,bulkFiles?[0,hash.slice(),total]:[hash.slice(),total],{signal});check();
 if(!Array.isArray(begin)||begin.length!==4||!integer(begin[1],0,total)||!integer(begin[2],0,1)||begin[3]!==chunkBytes||
  (begin[2]===1?(begin[0]!==null||begin[1]!==total):(!bytes(begin[0],16)||!begin[0].some(Boolean))))throw Error('Invalid track upload admission');
 const token=begin[0]?.slice();let offset=begin[1];onProgress?.({offset,total});
 if(begin[2]===0){
  while(offset<total){
   check();const chunk=file.slice(offset,Math.min(offset+chunkBytes,total)),next=offset+chunk.length;
   const response=await request(bulkFiles?TRACK_BULK_OPERATION:5,bulkFiles?[1,token.slice(),offset,chunk]:[token.slice(),offset,chunk],{signal});check();
   if(!Array.isArray(response)||response.length!==2||!equal(response[0],token)||response[1]!==next)throw Error('Invalid track upload acknowledgement');
   offset=next;onProgress?.({offset,total});
  }
  const sealed=await request(6,[token.slice()],{signal});check();
  if(!Array.isArray(sealed)||sealed.length!==4||!equal(sealed[0],hash)||sealed[1]!==total||sealed[2]!==1||!integer(sealed[3],0,1))throw Error('Invalid track upload seal');
 }
 return {hash,bytes:total};
}
