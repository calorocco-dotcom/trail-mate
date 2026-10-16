import {bytes,equal,integer} from './protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const uint64=value=>typeof value==='bigint'?value>0n&&value<=0xffffffffffffffffn:Number.isSafeInteger(value)&&value>0;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

// The UI retains this prepared operation before calling. A lost response or
// cancellation can leave a durable intent; never replace the ID on retry.
export async function commitDeviceDraft(session,prepared,author,{signal,onProgress=()=>{},resume=false,timeoutMs=90000,retryMs=250,
  sleep=delay,now=()=>performance.now()}={}){
  if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Error('发布提交前必须验证设备作者身份');
  if(!bytes(prepared?.prepareId,16)||!prepared.prepareId.some(byte=>byte)||!bytes(prepared.id,16)||!uint64(prepared.generation)||
     !bytes(prepared.authorKey,64)||!equal(prepared.authorKey,author.publicKey)||prepared.authorGeneration!==author.generation||
     !bytes(prepared.directory,16)||!bytes(prepared.directoryEpoch,16)||!bytes(prepared.contentHash,32)||
     !integer(prepared.fromRevision,0,0xffffffff)||!integer(prepared.toRevision,1,0xffffffff)||
     !(prepared.toRevision===prepared.fromRevision||(prepared.fromRevision!==0xffffffff&&prepared.toRevision===prepared.fromRevision+1))||
     !Number.isFinite(prepared.expiresAt))throw Error('发布提交与准备结果、作者或目录绑定不一致');
  if(typeof resume!=='boolean'||!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('发布提交时间预算无效');
  const binding={operationId:prepared.prepareId.slice(),id:prepared.id.slice(),generation:prepared.generation,
    directory:prepared.directory.slice(),contentHash:prepared.contentHash.slice(),revision:prepared.toRevision};
  const authorGeneration=author.generation,expiresAt=prepared.expiresAt,deadline=now()+timeoutMs;
  const expired=()=>{const error=Error('发布准备已过期，请重新核对准备结果后确认');error.code='PREPARE_EXPIRED';error.submitted=false;throw error;};
  const check=()=>{
    if(signal?.aborted)throw signal.reason??new DOMException('发布提交等待已取消','AbortError');
    if(now()>=deadline){const error=Error('发布提交结果尚未确认，请保留相同操作 ID 并查询设备任务');
      error.code='COMMIT_PENDING';error.operationId=binding.operationId.slice();throw error;}
  };
  check();if(!resume&&now()>=expiresAt)expired();
  onProgress({phase:resume?'resuming':'submitting',operationId:binding.operationId.slice()});
  check();
  if(!resume&&now()>=expiresAt)expired();
  for(;;){
    check();let reply;
    try{reply=await session.request(OPERATION.CACHE_COMMIT,[binding.operationId.slice(),binding.contentHash.slice(),authorGeneration]);}
    catch(error){check();if(error.code===MANAGEMENT_STATUS.BUSY){await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
      const unresolved=Error('发布提交结果尚未确认，请保留操作 ID 并查询原任务',{cause:error});
      unresolved.code=error.code??'COMMIT_UNKNOWN';unresolved.operationId=binding.operationId.slice();throw unresolved;}
    check();
    if(!Array.isArray(reply)||reply.length!==7)throw Error('设备发布准入回执无效；请保留操作 ID 查询原任务');
    const [operation,id,generation,directory,hash,revision,admitted]=reply;
    if(!bytes(operation,16)||!equal(operation,binding.operationId)||!bytes(id,16)||!equal(id,binding.id)||
       !uint64(generation)||BigInt(generation)!==BigInt(binding.generation)||!bytes(directory,16)||!equal(directory,binding.directory)||
       !bytes(hash,32)||!equal(hash,binding.contentHash)||revision!==binding.revision||admitted!==1)
      throw Error('设备发布准入回执与保留操作不一致；请查询原任务');
    // Admission is not a directory receipt. After the first request expiresAt
    // cannot invalidate a real admission; use JOB_GET to determine publication.
    return {...binding,localAdmitted:true,directoryConfirmed:false};
  }
}
