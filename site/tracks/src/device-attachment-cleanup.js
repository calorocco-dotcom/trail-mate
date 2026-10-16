import {decode,equal} from '../../geocaching/src/protocol.js';
import {OPERATION} from '../../device/src/management-protocol.js';
import {readTrackDraft} from './draft-store.js';
import {draftPackageInputs} from './draft-package.js';
import {buildDeviceAttachmentPlan} from './device-attachment-plan.js';
const digest=async value=>new Uint8Array(await crypto.subtle.digest('SHA-256',value));

// Called only after the device's durable publication acknowledgement is saved.
// Rebuild exact cleanup tuples from the retained signing inputs. Never create
// new upload IDs, enumerate unrelated staging, or remove retained package data.
export async function cleanupTrackAttachmentUploads(session,record,{readDraft=readTrackDraft,signal,onProgress=()=>{}}={}){
 const manifest=decode(record.plan.manifest,4096);
 if(manifest[0]!==2||manifest[15]===null)return;
 if(record.stage!=='confirmed')throw Error('Attachment cleanup requires confirmed publication');
 const snapshot=await readDraft(record.draftId);
 if(snapshot.version!==record.draftVersion||!snapshot.attachmentBindings)throw Error('Retained attachment cleanup inputs unavailable');
 if(snapshot.source.length!==record.plan.fileBytes||!equal(await digest(snapshot.source),record.plan.fileHash))throw Error('Attachment cleanup draft differs');
 const inputs=await draftPackageInputs(snapshot,record.plan);
 const plan=await buildDeviceAttachmentPlan(record.plan.manifest,record.plan.fileBytes,inputs);
 const items=[[plan.packageUploadId,10,plan.mapping], [plan.indexUploadId,8,plan.index],
  ...plan.resources.map(row=>[row.uploadId,9,row.content])];
 for(let index=0;index<items.length;index++){
  if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');
  const [id,kind,content]=items[index],hash=await digest(content);
  onProgress({phase:'cleaning',item:index+3});
  const response=await session.request(OPERATION.TRANSFER_CLEANUP,[id.slice(),kind,content.length,hash]);
  if(!Array.isArray(response)||response.length)throw Error('Invalid attachment cleanup acknowledgement');
 }
}
