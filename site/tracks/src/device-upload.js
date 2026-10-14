import {bytes,integer,equal,decode} from '../../geocaching/src/protocol.js';
import {validateTrackManifest} from './manifest.js';
import {OPERATION} from '../../device/src/management-protocol.js';
import {uploadObject} from '../../device/src/object-transfer.js';
const active=new WeakSet();
const digestDefault=async source=>new Uint8Array(await crypto.subtle.digest('SHA-256',source));
function validate(session,id){if(!session||typeof session.request!=='function'||!bytes(id,16)||!id.some(byte=>byte))throw Error('轨迹上传会话或 ID 无效');}
// Exact bytes only. KML/profile and author/revision preflight belong to the
// publication flow. A sealed candidate is not a signed/published track.
export async function uploadTrackFile(session,source,options={}){
 return uploadTrackBytes(session,source,3,8388608,options);
}
export async function uploadTrackManifest(session,source,options={}){
 validateTrackManifest(decode(source,4096));
 return uploadTrackBytes(session,source,4,4096,options);
}
export async function uploadTrackAttachmentIndex(session,source,options={}){return uploadTrackBytes(session,source,8,294912,options);}
export async function uploadTrackAttachmentResource(session,source,options={}){return uploadTrackBytes(session,source,9,8388608,options);}
export async function uploadTrackAttachmentBindings(session,source,options={}){return uploadTrackBytes(session,source,10,1100,options);}
export async function uploadTrackSignedManifest(session,source,options={}){return uploadTrackBytes(session,source,11,4166,options);}
async function uploadTrackBytes(session,source,kind,maximum,{id,digest=digestDefault,signal,onProgress,finishTimeoutMs,finishPollMs}={}){
 validate(session,id);
 if(!(source instanceof Uint8Array)||!source.length||source.length>maximum||typeof digest!=='function')throw Error('轨迹文件超过容量限制或输入无效');
 if(active.has(session))throw Error('当前设备已有轨迹上传任务');
 if(signal?.aborted)throw signal.reason??new DOMException('轨迹上传已暂停','AbortError');
 const content=source.slice(),transferId=id.slice();active.add(session);
 try{
  const hash=await digest(content.slice());
  if(signal?.aborted)throw signal.reason??new DOMException('轨迹上传已暂停','AbortError');
  if(!bytes(hash,32))throw Error('轨迹文件哈希计算失败');
  const boundedSession={async request(operation,body){
   const response=await session.request(operation,body);
   if(Array.isArray(response)&&response.length===4&&!integer(response[1],1,320))throw Error('设备轨迹块预算不符合 320 字节协议');
   return response;
  }};
  const receipt=await uploadObject(boundedSession,content,{id:transferId,kind,sha256:hash.slice(),signal,onProgress,finishTimeoutMs,finishPollMs});
  return {...receipt,sealed:true,published:false};
 }finally{active.delete(session);}
}
export async function queryTrackUpload(session,id,total){
 validate(session,id);if(!integer(total,1,8388608))throw Error('轨迹上传总长度无效');
 const transferId=id.slice(),response=await session.request(OPERATION.TRANSFER_STATUS,[transferId.slice()]);
 if(!Array.isArray(response)||response.length!==4||!bytes(response[0],16)||!equal(response[0],transferId)||!integer(response[1],1,320)||!integer(response[2],0,total)||!integer(response[3],0,1)||response[3]===1&&response[2]!==total)throw Error('设备轨迹上传状态无效');
 return {id:transferId,chunkBytes:response[1],offset:response[2],sealed:response[3]===1};
}
export async function discardTrackUpload(session,id){
 validate(session,id);if(active.has(session))throw Error('请先暂停轨迹上传，再回收暂存文件');
 const response=await session.request(OPERATION.TRANSFER_ABORT,[id.slice()]);
 if(!Array.isArray(response)||response.length)throw Error('设备轨迹回收回执无效');
 return true;
}
