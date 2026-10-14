import {Identity} from '@reticulum/core';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {parseTrackReference} from './shared-id.js';
import {verifyTrackManifest} from './manifest.js';
const fail=()=>{throw Object.assign(Error('Invalid author conflict evidence'),{code:'TRACK_CONFLICT_PROOF_INVALID'});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};

// The read adapter must authenticate the directory and correlate each response.
// A signed directory assertion alone is never an author-conflict proof.
export async function readTrackConflict(request,reference,{baseUrl,signal}={}){
 check(signal);let target;try{target=parseTrackReference(reference,{baseUrl});}catch{fail();}
 const objectId=target.id.slice(),body=await request(18,[objectId.slice()],{signal});check(signal);
 if(body===null)return null;
 if(!Array.isArray(body)||body.length!==3||!bytes(body[0],32)||!equal(body[0],objectId)||!bytes(body[1],32)||!bytes(body[2],32)||equal(body[1],body[2]))fail();
 const hashes=[body[1].slice(),body[2].slice()],versions=[];
 for(const hash of hashes){
  const response=await request(2,[objectId.slice(),hash.slice()],{signal});check(signal);
  if(!Array.isArray(response)||response.length!==2||!integer(response[1],0,1))fail();
  let verified;try{verified=await verifyTrackManifest(response[0]);}catch(error){check(signal);fail();}
  check(signal);if(!equal(verified.objectId,objectId)||!equal(verified.revisionHash,hash))fail();
  versions.push(verified);
 }
 const [a,b]=versions;
 if(a.record[4]!==b.record[4]||!equal(a.record[1],b.record[1])||!equal(a.record[2],b.record[2])||equal(a.manifest,b.manifest))fail();
 const fingerprint=await Identity.fullHash(a.record[1]);check(signal);
 return Object.freeze({objectId:objectId.slice(),authorFingerprint:fingerprint.slice(),revision:a.record[4],versions:Object.freeze(versions),authorSignaturesVerified:true,chainVerified:false});
}
