import {bytes,integer,equal} from '../../geocaching/src/protocol.js';
import {verifyTrackManifest,verifyTrackSuccessor} from './manifest.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const fail=(code,cause)=>{throw Object.assign(Error(code),{code,cause});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};

// Metadata only. This validates one ancestral path, not file availability,
// directory currentness, or the absence of other branches.
export async function readTrackVersionHistory(request,selected,{signal,maxVersions=64,onProgress}={}){
 check(signal);
 if(typeof request!=='function'||!integer(maxVersions,1,64)||!bytes(selected?.objectId,32)||!bytes(selected?.revisionHash,32))fail('TRACK_HISTORY_OPTIONS_INVALID');
 const objectId=selected.objectId.slice(),selectedHash=selected.revisionHash.slice();
 let head;try{head=await verifyTrackManifest([selected.manifest,selected.signature]);}catch(error){check(signal);fail('TRACK_HISTORY_PROOF_INVALID',error);}
 check(signal);if(!equal(head.objectId,objectId)||!equal(head.revisionHash,selectedHash))fail('TRACK_HISTORY_PROOF_INVALID');
 const versions=[head],seen=new Set([hex(head.revisionHash)]);
 const state=status=>Object.freeze({objectId:objectId.slice(),selectedHash:selectedHash.slice(),versions:Object.freeze(versions.map(value=>Object.freeze({record:structuredClone(value.record),manifest:value.manifest.slice(),signature:value.signature.slice(),objectId:value.objectId.slice(),revisionHash:value.revisionHash.slice()}))),status,complete:status==='complete',filesVerified:false,otherBranchesVerified:false});
 onProgress?.(state('reading'));
 while(versions.at(-1).record[4]>1){
  check(signal);if(versions.length>=maxVersions)return state('limited');
  const child=versions.at(-1),parentHash=child.record[5].slice();
  if(seen.has(hex(parentHash)))fail('TRACK_HISTORY_PROOF_INVALID');
  let response;try{response=await request(2,[objectId.slice(),parentHash.slice()],{signal});}
  catch(error){check(signal);if(error?.status===422&&['track missing','version missing'].includes(error.reason))return state('missing');throw error;}
  check(signal);if(!Array.isArray(response)||response.length!==2||!integer(response[1],0,1))fail('TRACK_HISTORY_PROOF_INVALID');
  let parent;try{parent=await verifyTrackManifest(response[0]);verifyTrackSuccessor(parent,child);}catch(error){check(signal);fail('TRACK_HISTORY_PROOF_INVALID',error);}
  check(signal);if(!equal(parent.objectId,objectId)||!equal(parent.revisionHash,parentHash))fail('TRACK_HISTORY_PROOF_INVALID');
  versions.push(parent);seen.add(hex(parent.revisionHash));onProgress?.(state('reading'));
 }
 check(signal);return state('complete');
}
