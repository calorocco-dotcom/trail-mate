import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {verifyTrackManifest} from './manifest.js';
import {verifyTrackReceipt} from './receipt.js';
import {uploadIPTrackFile} from './ip-file-upload.js';
import {matchTrackAttachmentIndex} from './attachment-index.js';
import {validateTrackPackageStatus} from './package-status.js';
import {freezeTrackAttachmentInputs} from './attachment-inputs.js';

export async function publishIPTrack(request,signed,source,{authorPublicKey,directoryPublicKey,directoryEpoch,requestId,signal,onProgress,packageSupported=false,bulkFiles=false,attachmentIndex,attachmentFiles=[]}={}){
 if(typeof request!=='function'||!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64)||
    !(source instanceof Uint8Array)||!source.length||source.length>8388608||!bytes(authorPublicKey,64)||!bytes(directoryPublicKey,64)||!bytes(directoryEpoch,16)||!bytes(requestId,16)||!requestId.some(Boolean))throw Error('Invalid track publication input');
 const content=signed.map(value=>value.slice()),file=source.slice(),author=authorPublicKey.slice(),directory=directoryPublicKey.slice(),epoch=directoryEpoch.slice(),id=requestId.slice();
 const {index:indexBytes,files:resources}=freezeTrackAttachmentInputs(attachmentIndex,attachmentFiles,source.length);
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};check();
 const verified=await verifyTrackManifest(content,{expectedAuthor:author}),[digest,total]=verified.record[13];
 if(total!==file.length||!equal(new Uint8Array(await crypto.subtle.digest('SHA-256',file)),digest))throw Error('Track file differs from signed manifest');check();
 let index=null;
 if(verified.record[0]===2){
  if(packageSupported!==true)throw Error('Track directory attachment capability unavailable');
  if(verified.record[15]){if(!(indexBytes instanceof Uint8Array))throw Error('Track attachment index required');index=(await matchTrackAttachmentIndex(indexBytes,content[0])).record;check();}
  else if(indexBytes!==undefined||resources.length)throw Error('Unbound track attachments');
 }else if(indexBytes!==undefined||resources.length)throw Error('Unbound track attachments');
 const byId=new Map();
 for(const resource of resources){
  if(byId.has(resource.id))throw Error('Duplicate track attachment input');const row=index?.[3].find(entry=>entry[0]===resource.id);
  if(!row||resource.content.length!==row[2]||!equal(new Uint8Array(await crypto.subtle.digest('SHA-256',resource.content)),row[1]))throw Error('Track attachment differs from signed index');check();byId.set(resource.id,resource.content);
 }
 // Verify local inputs before any upload. Missing supplied required files may
 // already exist at this directory; the final package query must prove that.
 await uploadIPTrackFile(request,file,digest,{signal,bulkFiles,onProgress:event=>onProgress?.({phase:'uploading',...event})});check();
 if(index){
  await uploadIPTrackFile(request,indexBytes,verified.record[15][0],{signal,bulkFiles,onProgress:event=>onProgress?.({phase:'resources',kind:'index',...event})});check();
  for(const row of index[3]){
   const source=byId.get(row[0]);if(!source)continue;
   await uploadIPTrackFile(request,source,row[1],{signal,bulkFiles,onProgress:event=>onProgress?.({phase:'resources',kind:'attachment',assetId:row[0],...event})});check();
  }
 }
 let availability;
 if(verified.record[0]===2){availability=validateTrackPackageStatus(await request(14,[content.map(value=>value.slice())],{signal}),verified.revisionHash,index);check();if(!availability.requiredComplete)throw Error('Required track attachments unavailable');}
 onProgress?.({phase:'publishing',offset:total,total});
 let response;
 try{response=await request(7,[content.map(value=>value.slice()),verified.record[5]?.slice()??null],{requestId:id.slice(),signal});}
 catch(error){throw Object.assign(Error(error.message),{code:'TRACK_PUBLICATION_UNCONFIRMED',requestId:id,cause:error});}
 if(!Array.isArray(response)||response.length!==2||!integer(response[1],0,1))throw Object.assign(Error('Invalid track publication response'),{code:'TRACK_PUBLICATION_UNCONFIRMED',requestId:id});
 const receipt=await verifyTrackReceipt(response[0],{directoryPublicKey:directory,directoryEpoch:epoch,requestId:id,manifestSigned:content});check();
 onProgress?.({phase:'confirmed',offset:total,total});return {...receipt,current:response[1]===1,...(availability?{availability}:{})};
}
