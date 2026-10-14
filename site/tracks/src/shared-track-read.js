import {Identity} from '@reticulum/core';
import {bytes,integer,equal} from '../../geocaching/src/protocol.js';
import {parseTrackReference} from './shared-id.js';
import {verifyTrackManifest} from './manifest.js';
import {TRACK_BULK_CHUNK_BYTES} from './bulk-file-profile.js';

const fail=code=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};

// request is the authenticated directory adapter's operation/body call. It must
// correlate responses to the selected directory and request before returning a
// body. Directory operations 2 and 8 match trailmate.track.v1, not USB opcodes.
export async function readSharedTrack(request,reference,{baseUrl,signal,maxBytes=8388608,chunkBytes=1024,download=true,onProgress}={}){
 check(signal);
 if(typeof request!=='function'||!integer(maxBytes,1,8388608)||!integer(chunkBytes,1,TRACK_BULK_CHUNK_BYTES))fail('TRACK_READ_OPTIONS_INVALID');
 let target;try{target=parseTrackReference(reference,{baseUrl});}catch{fail('TRACK_REFERENCE_INVALID');}
 const response=await request(2,[target.id.slice(),target.revision?.slice()??null],{signal});
 check(signal);
 if(!Array.isArray(response)||response.length!==2||!integer(response[1],0,1))fail('TRACK_MANIFEST_RESPONSE_INVALID');
 let verified;try{verified=await verifyTrackManifest(response[0]);}catch(error){check(signal);throw Object.assign(Error('TRACK_MANIFEST_INVALID'),{code:'TRACK_MANIFEST_INVALID',cause:error});}
 check(signal);
 if(!equal(verified.objectId,target.id)||target.revision&&!equal(verified.revisionHash,target.revision))fail('TRACK_REFERENCE_MISMATCH');
 const result={...verified,current:response[1]===1};
 if(!download)return result;
 const [digest,total]=verified.record[13];
 if(total>maxBytes)fail('TRACK_FILE_TOO_LARGE');
 const source=new Uint8Array(total);
 onProgress?.({phase:'downloading',offset:0,total});
 let offset=0;
 while(offset<total){
   check(signal);
   const length=Math.min(chunkBytes,total-offset),chunk=await request(8,[digest.slice(),offset,length],{signal});
   check(signal);
   if(!Array.isArray(chunk)||chunk.length!==4||!bytes(chunk[0],32)||!equal(chunk[0],digest)||chunk[1]!==total||chunk[2]!==offset||!(chunk[3] instanceof Uint8Array)||chunk[3].length!==length)fail('TRACK_FILE_RESPONSE_INVALID');
   source.set(chunk[3],offset);offset+=length;
   onProgress?.({phase:'downloading',offset,total});
 }
 check(signal);
 const actual=await Identity.fullHash(source);
 check(signal);
 if(!equal(actual,digest))fail('TRACK_FILE_HASH_MISMATCH');
 onProgress?.({phase:'verified',offset:total,total});
 check(signal);
 return {...result,source};
}
