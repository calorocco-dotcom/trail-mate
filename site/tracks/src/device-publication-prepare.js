import {bytes,equal,integer,decode} from '../../geocaching/src/protocol.js';
import {validateTrackManifest} from './manifest.js';
import {uploadTrackFile,uploadTrackManifest} from './device-upload.js';
import {prepareTrack} from './device-prepare.js';
import {buildDeviceAttachmentPlan,uploadDeviceAttachmentPlan} from './device-attachment-plan.js';
import {prepareTrackPackage} from './device-package-prepare.js';

const utf8=new TextEncoder();
const digest=async value=>new Uint8Array(await crypto.subtle.digest('SHA-256',value));
const joined=(prefix,...parts)=>{const domain=utf8.encode(prefix+'\0'),output=new Uint8Array(domain.length+parts.reduce((n,p)=>n+p.length,0));output.set(domain);let offset=domain.length;for(const part of parts){output.set(part,offset);offset+=part.length;}return output;};
const active=new WeakSet();

// The caller persists these three IDs with the exact draft bytes for resumption.
// No cleanup on failure: the device's durable uploads remain retryable.
export async function prepareTrackPublication(session,{kml,manifest,author,operationId,kmlUploadId,manifestUploadId,...attachments},
  {signal,onProgress=()=>{},prepareOptions={},operations=[]}={}){
  if(!session||typeof session.request!=='function'||!author?.verified||!bytes(author.publicKey,64)||
     !integer(author.generation,1,0xffffffff)||!(kml instanceof Uint8Array)||!kml.length||kml.length>8388608||
     !(manifest instanceof Uint8Array)||!manifest.length||manifest.length>4096||
     ![operationId,kmlUploadId,manifestUploadId].every(id=>bytes(id,16)&&id.some(v=>v))||equal(kmlUploadId,manifestUploadId))throw Error('轨迹准备需要已验证的设备身份、文件和持久操作 ID');
  if(active.has(session))throw Error('当前设备已有轨迹准备流程');
  const content=kml.slice(),raw=manifest.slice(),key=author.publicKey.slice(),generation=author.generation;
  const operation=operationId.slice(),kmlId=kmlUploadId.slice(),manifestId=manifestUploadId.slice();
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('轨迹准备已暂停','AbortError');};
  active.add(session);
  try{
    check();const record=validateTrackManifest(decode(raw,4096));
    let packagePlan;
    if(record[0]===2){
      if(!operations.includes(99))throw Error('Device track package capability unavailable');
      if(record[15])packagePlan=await buildDeviceAttachmentPlan(raw,content.length,attachments);
      else if(attachments.attachmentIndex!==undefined||attachments.attachmentFiles?.length||attachments.attachmentUploadIds?.length||attachments.packageUploadId!==undefined&&attachments.packageUploadId!==null)throw Error('Unbound device attachments');
    }else if(attachments.attachmentIndex!==undefined||attachments.attachmentFiles?.length||attachments.attachmentUploadIds?.length)throw Error('Unbound device attachments');
    if(!equal(record[1],key))throw Error('轨迹清单不属于当前设备身份');
    if(record[13][1]!==content.length||!equal(record[13][0],await digest(content)))throw Error('轨迹文件与清单摘要不一致');
    const objectId=await digest(joined('TrailMate.Track.ID.v1',key,record[2]));
    const revisionHash=await digest(joined(record[0]===2?'TrailMate.Track.Revision.v2':'TrailMate.Track.Revision.v1',raw));check();
    await uploadTrackFile(session,content,{id:kmlId,signal,onProgress:progress=>onProgress({phase:'uploading-kml',...progress})});check();
    await uploadTrackManifest(session,raw,{id:manifestId,signal,onProgress:progress=>onProgress({phase:'uploading-manifest',...progress})});check();
    if(packagePlan){await uploadDeviceAttachmentPlan(session,packagePlan,{signal,onProgress});check();}
    const bindings={operationId:operation,kmlUploadId:kmlId,manifestUploadId:manifestId,generation,parentHash:record[5],objectId,revisionHash,revision:record[4]};
    const options={...prepareOptions,signal,onProgress};
    return record[0]===2?await prepareTrackPackage(session,bindings,packagePlan?.packageUploadId??null,options):await prepareTrack(session,bindings,options);
  }finally{active.delete(session);}
}
