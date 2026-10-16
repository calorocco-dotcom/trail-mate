import {bytes,equal,encode} from '../../geocaching/src/protocol.js';
import {freezeTrackAttachmentInputs} from './attachment-inputs.js';
import {matchTrackAttachmentIndex} from './attachment-index.js';
import {uploadTrackAttachmentIndex,uploadTrackAttachmentResource,uploadTrackAttachmentBindings} from './device-upload.js';
const validId=id=>bytes(id,16)&&id.some(Boolean);
const digest=async source=>new Uint8Array(await crypto.subtle.digest('SHA-256',source));

// All inputs, supplied files, ID mapping and required slots are admitted and
// checked before the caller sends any USB upload. URLs never become paths.
export async function buildDeviceAttachmentPlan(raw,kmlBytes,{attachmentIndex,attachmentFiles=[],attachmentUploadIds=[],indexUploadId,packageUploadId}={}){
 const frozen=freezeTrackAttachmentInputs(attachmentIndex,attachmentFiles,kmlBytes);
 if(!validId(indexUploadId)||!validId(packageUploadId)||!Array.isArray(attachmentUploadIds)||attachmentUploadIds.length>64||
    attachmentUploadIds.some(row=>!row||typeof row.id!=='string'||!validId(row.uploadId)))throw Error('Invalid device attachment bindings');
 const indexId=indexUploadId.slice(),mappingId=packageUploadId.slice(),ids=attachmentUploadIds.map(row=>({id:row.id,uploadId:row.uploadId.slice()}));
 const manifest=raw.slice();
 const {record:index}=await matchTrackAttachmentIndex(frozen.index,manifest);
 const byId=new Map(),files=new Map();
 for(const row of ids){if(byId.has(row.id)||!index[3].some(entry=>entry[0]===row.id))throw Error('Invalid device attachment ID mapping');byId.set(row.id,row.uploadId);}
 for(const row of frozen.files){
  if(files.has(row.id)||!byId.has(row.id))throw Error('Device attachment file lacks a unique upload binding');
  const declared=index[3].find(entry=>entry[0]===row.id);
  if(!declared||row.content.length!==declared[2]||!equal(await digest(row.content),declared[1]))throw Error('Device attachment differs from manifest index');
  files.set(row.id,row.content);
 }
 const packed=new Uint8Array(index[3].length*16),resources=[];
 for(let i=0;i<index[3].length;i++){
  const row=index[3][i],id=byId.get(row[0]);
  if(!id){if(row[6]===1)throw Error('Required device attachment upload missing');continue;}
  packed.set(id,i*16);
  const content=files.get(row[0]);if(content)resources.push({id:row[0],uploadId:id.slice(),content});
 }
 const mapping=encode([1,indexId.slice(),packed]);if(mapping.length>1100)throw Error('Device attachment bindings exceed budget');
 return {index:frozen.index,indexUploadId:indexId,packageUploadId:mappingId,mapping,resources};
}
export async function uploadDeviceAttachmentPlan(session,plan,{signal,onProgress=()=>{}}={}){
 await uploadTrackAttachmentIndex(session,plan.index,{id:plan.indexUploadId,signal,onProgress:event=>onProgress({phase:'uploading-index',...event})});
 for(const row of plan.resources)await uploadTrackAttachmentResource(session,row.content,{id:row.uploadId,signal,onProgress:event=>onProgress({phase:'uploading-resource',assetId:row.id,...event})});
 // Last sealed upload is the mapping the prepare request explicitly binds.
 await uploadTrackAttachmentBindings(session,plan.mapping,{id:plan.packageUploadId,signal,onProgress:event=>onProgress({phase:'uploading-bindings',...event})});
}
