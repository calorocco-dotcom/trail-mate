import {bytes,equal,encode} from '../../geocaching/src/protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {validateAuthorInfo} from '../../device/src/author-identity.js';
import {verifyTrackManifest} from './manifest.js';
import {freezeTrackAttachmentInputs} from './attachment-inputs.js';
import {buildDeviceAttachmentPlan,uploadDeviceAttachmentPlan} from './device-attachment-plan.js';
import {uploadTrackFile,uploadTrackSignedManifest} from './device-upload.js';
const active=new WeakSet(),validId=id=>bytes(id,16)&&id.some(Boolean),digest=async source=>new Uint8Array(await crypto.subtle.digest('SHA-256',source));
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
// Local device identity authorizes storage. Preserve the original signed author
// and bind this installation to the exact immutable object/revision selected.
export async function installDeviceTrackPackage(session,input,{operations=[],signal,onProgress=()=>{},timeoutMs=900000,retryMs=500,sleep=delay,now=()=>performance.now()}={}){
 if(!Array.isArray(operations)||![32,33,35,100].every(op=>operations.includes(op)))throw Error('Device package installation capability unavailable');
 if(!session||typeof session.request!=='function'||active.has(session)||!input?.author?.verified||!bytes(input.author.publicKey,64)||!Number.isInteger(input.author.generation)||input.author.generation<1||input.author.generation>0xffffffff||
  !Array.isArray(input.signed)||input.signed.length!==2||!(input.signed[0] instanceof Uint8Array)||!input.signed[0].length||input.signed[0].length>4096||!bytes(input.signed[1],64)||
  !(input.kml instanceof Uint8Array)||!input.kml.length||input.kml.length>8388608||![input.operationId,input.kmlUploadId,input.signedUploadId].every(validId)||equal(input.kmlUploadId,input.signedUploadId))throw Error('Invalid device package installation input');
 if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0||retryMs>=30000||typeof sleep!=='function'||typeof now!=='function'||typeof onProgress!=='function')throw Error('Invalid package installation timing');
 const attachments=freezeTrackAttachmentInputs(input.attachmentIndex,input.attachmentFiles??[],input.kml.length);
 if(input.attachmentUploadIds!==undefined&&(!Array.isArray(input.attachmentUploadIds)||input.attachmentUploadIds.length>64||input.attachmentUploadIds.some(row=>!row||typeof row.id!=='string'||!validId(row.uploadId))))throw Error('Invalid device resource mapping');
 const frozen={...input,attachmentIndex:attachments.index,attachmentFiles:attachments.files,
  attachmentUploadIds:input.attachmentUploadIds?.map(row=>({id:row.id,uploadId:row.uploadId.slice()})),indexUploadId:input.indexUploadId?.slice(),packageUploadId:input.packageUploadId?.slice()};
 const signed=input.signed.map(value=>value.slice()),kml=input.kml.slice(),author=input.author.publicKey.slice(),generation=input.author.generation;
 const operation=input.operationId.slice(),kmlId=input.kmlUploadId.slice(),signedId=input.signedUploadId.slice();
 const expectedObject=input.objectId?.slice(),expectedRevision=input.revisionHash?.slice();
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Installation paused','AbortError');};check();active.add(session);
 try{
  const verified=await verifyTrackManifest(signed);check();
  if(expectedObject!==undefined&&(!bytes(expectedObject,32)||!equal(expectedObject,verified.objectId))||expectedRevision!==undefined&&(!bytes(expectedRevision,32)||!equal(expectedRevision,verified.revisionHash)))throw Error('Selected package object/revision changed');
  if(kml.length!==verified.record[13][1]||!equal(await digest(kml),verified.record[13][0]))throw Error('Package KML differs from original signed manifest');check();
  let plan;
  if(verified.record[0]===2&&verified.record[15])plan=await buildDeviceAttachmentPlan(signed[0],kml.length,frozen);
  else if(frozen.attachmentIndex!==undefined||frozen.attachmentFiles.length||frozen.attachmentUploadIds?.length||frozen.packageUploadId!==undefined&&frozen.packageUploadId!==null)throw Error('Unbound package resources');
  check();const current=await validateAuthorInfo(await session.request(OPERATION.AUTHOR_INFO,[]));check();
  if(current.generation!==generation||!equal(current.publicKey,author))throw Object.assign(Error('Device identity changed'),{code:MANAGEMENT_STATUS.STALE_IDENTITY});
  const encoded=encode(signed);if(encoded.length>4166)throw Error('Signed package manifest exceeds capacity');
  await uploadTrackFile(session,kml,{id:kmlId,signal,onProgress:event=>onProgress({phase:'uploading-kml',...event})});check();
  await uploadTrackSignedManifest(session,encoded,{id:signedId,signal,onProgress:event=>onProgress({phase:'uploading-signed-manifest',...event})});check();
  if(plan){await uploadDeviceAttachmentPlan(session,plan,{signal,onProgress});check();}
  const deadline=now()+timeoutMs,target='/routes/p-'+Array.from(verified.revisionHash,byte=>byte.toString(16).padStart(2,'0')).join('')+'.kml';
  for(;;){
   check();if(now()>=deadline)throw Error('Package installation unconfirmed; retain operation and upload IDs for retry');
   let reply,busy=false;
   try{reply=await session.request(OPERATION.TRACK_PACKAGE_INSTALL,[operation.slice(),kmlId.slice(),signedId.slice(),generation,null,plan?.packageUploadId.slice()??null,verified.objectId.slice(),verified.revisionHash.slice()]);}
   catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;busy=true;}
   check();
   if(!busy){
    if(!Array.isArray(reply)||reply.length!==5||!bytes(reply[0],16)||!equal(reply[0],operation)||(reply[1]!==0&&reply[1]!==1))throw Error('Invalid package installation response');
    if(reply[1]===1){if(!bytes(reply[2],32)||!equal(reply[2],verified.objectId)||!bytes(reply[3],32)||!equal(reply[3],verified.revisionHash)||reply[4]!==target)throw Error('Installed package binding mismatch');
     onProgress({phase:'installed',target});return {installed:true,target,operationId:operation,generation,objectId:verified.objectId.slice(),revisionHash:verified.revisionHash.slice(),signed,verified};}
    if(reply.slice(2).some(value=>value!==null))throw Error('Invalid pending package installation response');
   }
   onProgress({phase:'installing'});await sleep(Math.min(retryMs,Math.max(1,deadline-now())));
  }
 }finally{active.delete(session);}
}
