import {OPERATION} from '../../device/src/management-protocol.js';
import {validateAuthorInfo} from '../../device/src/author-identity.js';
import {bytes,equal,decode} from '../../geocaching/src/protocol.js';
import {prepareTrackPublication} from './device-publication-prepare.js';
import {verifyTrackManifest} from './manifest.js';
import {freezeTrackAttachmentInputs} from './attachment-inputs.js';
const active=new WeakSet();

export async function signDeviceTrackManifest(session,input,{operations=[],...options}={}){
 if(!Array.isArray(operations))throw Error('Invalid device capabilities');const supported=operations.slice();
 const packageMode=input?.manifest instanceof Uint8Array&&decode(input.manifest,4096)?.[0]===2;
 if(![32,33,35,packageMode?OPERATION.TRACK_PACKAGE_SIGN_PREPARE:OPERATION.TRACK_SIGN_PREPARE].every(op=>supported.includes(op)))throw Error('Device local track signature capability unavailable');
 if(!session||typeof session.request!=='function'||active.has(session)||!input?.author?.verified||!bytes(input.author.publicKey,64)||
    !(input.manifest instanceof Uint8Array)||!(input.kml instanceof Uint8Array))throw Error('Invalid local track signing input');
 const attachments=freezeTrackAttachmentInputs(input.attachmentIndex,input.attachmentFiles??[],input.kml.length);
 if(input.attachmentUploadIds!==undefined&&(!Array.isArray(input.attachmentUploadIds)||input.attachmentUploadIds.length>64||input.attachmentUploadIds.some(row=>!row||typeof row.id!=='string'||!bytes(row.uploadId,16))))throw Error('Invalid device attachment IDs');
 const frozen={...input,kml:input.kml.slice(),manifest:input.manifest.slice(),author:{...input.author,publicKey:input.author.publicKey.slice()},
  attachmentIndex:attachments.index,attachmentFiles:attachments.files,
  attachmentUploadIds:input.attachmentUploadIds?.map(row=>({id:row.id,uploadId:row.uploadId.slice()})),
  indexUploadId:input.indexUploadId?.slice(),packageUploadId:input.packageUploadId?.slice(),
  operationId:input.operationId?.slice(),kmlUploadId:input.kmlUploadId?.slice(),manifestUploadId:input.manifestUploadId?.slice()};
 active.add(session);
 try{
  if(options.signal?.aborted)throw options.signal.reason??new DOMException('Paused','AbortError');
  const author=await validateAuthorInfo(await session.request(OPERATION.AUTHOR_INFO,[]));
  if(!equal(author.publicKey,frozen.author.publicKey)||author.generation!==frozen.author.generation)throw Object.assign(Error('Device identity changed'),{code:'STALE_IDENTITY'});
  let signature;
  const local={request:async(op,body)=>{
   if(op!==OPERATION.TRACK_PREPARE)return session.request(op,body);
   const reply=await session.request(OPERATION.TRACK_SIGN_PREPARE,body);
   if(!Array.isArray(reply)||reply.length!==6||(reply[1]===1?!bytes(reply[5],64):reply[5]!==null))throw Error('Invalid local track signature response');
   if(reply[1]===1)signature=reply[5].slice();return reply.slice(0,5);
  }};
  const prepared=await prepareTrackPublication(local,frozen,{...options,operations:supported});
  if(packageMode)signature=prepared.signature;
  if(!signature)throw Error('Device did not return a prepared track signature');
  const signed=[frozen.manifest.slice(),signature],verified=await verifyTrackManifest(signed,{expectedAuthor:frozen.author.publicKey});
  if(!equal(verified.objectId,prepared.objectId)||!equal(verified.revisionHash,prepared.revisionHash))throw Error('Device signed different track content');
  return {...prepared,signed,verified};
 }finally{active.delete(session);}
}
