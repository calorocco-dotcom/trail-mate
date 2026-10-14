import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const nonzero=(value,size)=>bytes(value,size)&&value.some(byte=>byte);
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

// Retain this exact prepared operation and directory before submitting. A lost
// reply can leave a durable device task; retries must never generate a new ID.
export async function commitTrack(session,prepared,directory,author,{signal,timeoutMs=90000,retryMs=250,
  sleep=delay,now=()=>performance.now(),onProgress=()=>{}}={}){
  if(!session||typeof session.request!=='function'||!author?.verified||!nonzero(author.publicKey,64)||
     !integer(author.generation,1,0xffffffff)||prepared?.prepared!==true||!nonzero(prepared.operationId,16)||
     !nonzero(prepared.objectId,32)||!nonzero(prepared.revisionHash,32)||!integer(prepared.revision,1,0xffffffff)||
     prepared.generation!==author.generation)throw Error('轨迹提交需要与当前设备身份一致的准备结果');
  if(!directory?.capabilitiesVerified||directory.protocol!=='trailmate.track.v1'||!nonzero(directory.delivery,16)||!nonzero(directory.epoch,16))throw Error('轨迹提交需要已验证的轨迹目录');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0||
     typeof sleep!=='function'||typeof now!=='function'||typeof onProgress!=='function')throw Error('轨迹提交时间预算无效');
  const binding={operationId:prepared.operationId.slice(),objectId:prepared.objectId.slice(),revisionHash:prepared.revisionHash.slice(),
    generation:author.generation,revision:prepared.revision,directory:directory.delivery.slice(),directoryEpoch:directory.epoch.slice()};
  const deadline=now()+timeoutMs;let sent=false;
  const unresolved=cause=>{const error=Error('轨迹提交结果尚未确认，请保留操作 ID 并查询原任务',{cause});error.code='COMMIT_PENDING';error.operationId=binding.operationId.slice();return error;};
  const check=()=>{if(signal?.aborted){if(sent)throw unresolved(signal.reason);throw signal.reason??new DOMException('轨迹提交等待已暂停','AbortError');}if(now()>=deadline)throw unresolved();};
  check();onProgress({phase:'submitting',operationId:binding.operationId.slice()});
  for(;;){
    check();let reply;
    try{sent=true;reply=await session.request(OPERATION.TRACK_COMMIT,[binding.operationId.slice(),binding.objectId.slice(),binding.revisionHash.slice(),
      binding.generation,binding.revision,binding.directory.slice(),binding.directoryEpoch.slice()],{signal});}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw unresolved(error);await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(reply)||reply.length!==8||!bytes(reply[0],16)||!equal(reply[0],binding.operationId)||
       !bytes(reply[1],32)||!equal(reply[1],binding.objectId)||!bytes(reply[2],32)||!equal(reply[2],binding.revisionHash)||
       reply[3]!==binding.generation||reply[4]!==binding.revision||!bytes(reply[5],16)||!equal(reply[5],binding.directory)||
       !bytes(reply[6],16)||!equal(reply[6],binding.directoryEpoch)||reply[7]!==1)throw unresolved(Error('设备准入回执绑定无效'));
    return {...binding,localAdmitted:true,directoryConfirmed:false,published:false};
  }
}
