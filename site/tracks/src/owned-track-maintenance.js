import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {parseTrackReference,trackId,trackShareLink} from './shared-id.js';
import {readSharedTrack} from './shared-track-read.js';
import {createTrackSuccessorPlan} from './publication-successor.js';
import {readSharedTrackPackage} from './shared-track-package.js';
import {verifyInstallPayload} from './package-install-store.js';
import {buildDraftAttachmentIndex} from './draft-attachments.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};

// request belongs to one authenticated directory. A current head assertion is
// valid only for this read; device preparation and publish CAS recheck it.
export async function createOwnedTrackMaintenancePlan(request,reference,author,{archive=false,kml,baseUrl,signal,maxBytes=8388608,chunkBytes=1024,onProgress,creationOptions={},packageSupported=false}={}){
 check(signal);
 if(typeof request!=='function'||!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff)||typeof archive!=='boolean'||!archive&&(!(kml instanceof Uint8Array)||!kml.length||kml.length>maxBytes))fail('TRACK_MAINTENANCE_OPTIONS_INVALID');
 const retainedAuthor={verified:true,publicKey:author.publicKey.slice(),generation:author.generation},content=kml?.slice();
 const target=parseTrackReference(reference,{baseUrl});
 // Always ask for the current head, even when the user opened a pinned link.
 const head=await readSharedTrack(request,trackId(target.id),{signal,download:false,maxBytes,chunkBytes});check(signal);
 if(!head.current)fail('TRACK_HEAD_UNCONFIRMED');
 if(target.revision&&!equal(target.revision,head.revisionHash))fail('TRACK_HEAD_CHANGED');
 if(!equal(head.record[1],retainedAuthor.publicKey))fail('TRACK_NOT_OWNED');
 if(head.record[6]!==0)fail('TRACK_ALREADY_ARCHIVED');
 let source=content;
 let packageSource,packageOptions={};
 if(head.record[0]===2&&head.record[15]){
  const pinned=trackShareLink(baseUrl??'https://trailmate.invalid/',head.objectId,{revision:head.revisionHash});
  const packet=await readSharedTrackPackage(request,pinned,{baseUrl,signal,maxBytes,chunkBytes,onProgress,packageSupported});check(signal);
  if(!packet.detail.current||!equal(packet.detail.revisionHash,head.revisionHash))fail('TRACK_HEAD_CHANGED');
  packageSource={signed:[packet.detail.manifest,packet.detail.signature],kml:packet.detail.source,attachmentIndex:packet.attachmentIndex,attachmentFiles:packet.files};
  const checked=await verifyInstallPayload(packageSource);if(archive)source=checked.payload.kml;
  const rows=checked.payload.attachmentFiles.map(file=>{const entry=checked.index[3].find(row=>row[0]===file.id);return {id:file.id,content:file.content,mime:entry[3],reference:entry[4],role:entry[5],required:entry[6]===1};});
  const next=await buildDraftAttachmentIndex(source,rows,checked.payload.attachmentIndex);
  packageOptions={assets:next.assets};
 }else if(archive){
  const pinned=trackShareLink(baseUrl??'https://trailmate.invalid/',head.objectId,{revision:head.revisionHash});
  const downloaded=await readSharedTrack(request,pinned,{signal,download:true,maxBytes,chunkBytes,onProgress});check(signal);
  if(!downloaded.current||!equal(downloaded.revisionHash,head.revisionHash))fail('TRACK_HEAD_CHANGED');source=downloaded.source;
 }
 const signed=[head.manifest.slice(),head.signature.slice()];
 const plan=await createTrackSuccessorPlan(source,retainedAuthor,signed,{...creationOptions,...packageOptions,archive,signal});check(signal);
 return {plan,source:source.slice(),previousSigned:signed,objectId:head.objectId.slice(),parentHash:head.revisionHash.slice(),revision:head.record[4]+1,archive,packageSource};
}
