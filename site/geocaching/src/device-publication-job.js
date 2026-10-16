import {bytes,equal,integer} from './protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';

export const PUBLICATION_PHASE = Object.freeze({PREPARING:0,PERSISTING:1,WAITING:2,
  CONFIRMED:3,STOPPED:4,FAILED:5,STOPPING:6});
const known=value=>value===null||value===0||value===1;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

// Query one snapshot; a Waiting result is returned to the UI, not hidden by
// polling until success. Busy concerns only the bounded storage read lease.
export async function readPublicationJob(session,binding,author,{signal,stop=false,timeoutMs=90000,retryMs=250,sleep=delay,
  now=()=>performance.now()}={}){
  if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Error('查询发布任务前必须验证设备作者身份');
  if(!bytes(binding?.operationId,16)||!binding.operationId.some(byte=>byte)||
     (binding.cacheId!=null&&!bytes(binding.cacheId,32))||(binding.revisionHash!=null&&!bytes(binding.revisionHash,32))||
     (binding.revision!=null&&!integer(binding.revision,1,0xffffffff)))throw Error('发布任务查询绑定无效');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('发布任务查询时间预算无效');
  const retained={operationId:binding.operationId.slice(),cacheId:binding.cacheId?.slice()??null,
    revisionHash:binding.revisionHash?.slice()??null,revision:binding.revision??null};
  const generation=author.generation,deadline=now()+timeoutMs;
  const check=()=>{
    if(signal?.aborted)throw signal.reason??new DOMException('发布任务查询已取消','AbortError');
    if(now()>=deadline){const error=Error('发布任务状态尚未确认，请保留相同操作 ID 并重新连接设备查询');
      error.code='JOB_PENDING';error.operationId=retained.operationId.slice();throw error;}
  };
  for(;;){
    check();let reply;
    try{reply=await session.request(stop?OPERATION.JOB_CANCEL:OPERATION.JOB_GET,[retained.operationId.slice(),generation]);}
    catch(error){
      check();
      if(error.code===MANAGEMENT_STATUS.BUSY){await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
      if(error.code===MANAGEMENT_STATUS.EXPIRED){
        const unknown=Error('设备尚未找到此发布任务的保留结果，请保留操作 ID；不能据此重新发布',{cause:error});
        unknown.code='JOB_UNKNOWN';unknown.operationId=retained.operationId.slice();throw unknown;
      }
      throw error;
    }
    check();
    const result=decodePublicationJob(reply,retained);
    if(stop&&result.phase!==PUBLICATION_PHASE.STOPPED&&result.phase!==PUBLICATION_PHASE.CONFIRMED)
      throw Error('设备尚未确认停止原任务，请保留操作 ID 并继续查询');
    return result;
  }
}

export function stopPublicationJob(session,binding,author,options={}){
  return readPublicationJob(session,binding,author,{...options,stop:true});
}

// A queried row is evidence about the retained operation, not permission to
// create another revision. Neither transport confirmation nor cancellation
// proves the directory's final decision. Unknown evidence stays null.
export function decodePublicationJob(reply,{operationId,cacheId=null,revisionHash=null,revision=null}={}){
  if(!bytes(operationId,16)||!operationId.some(byte=>byte)||
     (cacheId!==null&&!bytes(cacheId,32))||(revisionHash!==null&&!bytes(revisionHash,32))||
     (revision!==null&&!integer(revision,1,0xffffffff)))throw Error('发布任务查询绑定无效');
  if(!Array.isArray(reply)||reply.length!==8)throw Error('设备发布任务回执无效');
  const [id,phase,cache,hash,version,durable,transported,error]=reply;
  if(!bytes(id,16)||!equal(id,operationId)||!integer(phase,0,6)||
     (cache!==null&&!bytes(cache,32))||(hash!==null&&!bytes(hash,32))||
     (cache===null)!==(hash===null)||!integer(version,1,0xffffffff)||
     !known(durable)||!known(transported)||
     (error!==null&&(typeof error!=='string'||!error.length||new TextEncoder().encode(error).length>128)))
    throw Error('设备发布任务回执无效或与查询不一致');
  // Once the immutable signed attempt exists its identifiers must match the
  // operation the user is following. Before admission they may still be null.
  if((cacheId!==null&&cache!==null&&!equal(cacheId,cache))||
     (revisionHash!==null&&hash!==null&&!equal(revisionHash,hash))||
     (revision!==null&&version!==revision))throw Error('设备发布任务内容与保留的操作不一致');
  if((phase===PUBLICATION_PHASE.WAITING&&(cache===null||durable!==1))||
     (phase===PUBLICATION_PHASE.CONFIRMED&&(cache===null||durable!==1||transported!==1||error!==null))||
     (phase===PUBLICATION_PHASE.STOPPED&&(cache===null||durable!==1))||
     (transported===1&&durable===0))throw Error('设备发布任务证据不一致');
  return Object.freeze({operationId:id.slice(),phase,cacheId:cache?.slice()??null,
    revisionHash:hash?.slice()??null,revision:version,
    durableConfirmed:durable===null?null:durable===1,
    transportConfirmed:transported===null?null:transported===1,
    directoryConfirmed:phase===PUBLICATION_PHASE.CONFIRMED,error,
    terminal:phase===PUBLICATION_PHASE.CONFIRMED||phase===PUBLICATION_PHASE.STOPPED||phase===PUBLICATION_PHASE.FAILED});
}
