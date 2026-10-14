import {requestTrackJob} from './device-job.js';
import {encodeTrackJobBinding,decodeTrackJobBinding} from './job-binding.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
export async function cancelTrackJob(session,binding,{signal,timeoutMs=20000,retryMs=250,sleep=delay,now=()=>performance.now()}={}){
 if(!session||typeof session.request!=='function'||!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0||
  typeof sleep!=='function'||typeof now!=='function')throw Error('轨迹取消参数无效');
 const retained=decodeTrackJobBinding(encodeTrackJobBinding(binding)),deadline=now()+timeoutMs;let sent=false;
 const pending=cause=>{const error=Error('取消结果尚未确认，请用原操作 ID 查询设备任务。',{cause});error.code='JOB_CANCEL_PENDING';error.operationId=retained.operationId.slice();return error;};
 const check=()=>{if(signal?.aborted){if(sent)throw pending(signal.reason);throw signal.reason??new DOMException('取消等待已暂停','AbortError');}if(now()>=deadline)throw pending();};
 for(;;){
  check();let job;
  try{sent=true;job=await requestTrackJob(session,retained,OPERATION.JOB_CANCEL,{signal});}
  catch(error){check();if(error.code===MANAGEMENT_STATUS.BUSY){await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
   if([MANAGEMENT_STATUS.CONFLICT,MANAGEMENT_STATUS.UNSUPPORTED,MANAGEMENT_STATUS.STALE_IDENTITY].includes(error.code))throw error;
   throw pending(error);}
  check();if(job.phase!=='cancelled'||job.active||job.published)throw pending(Error('设备未返回已停止的取消状态'));
  return {...job,cancelled:true};
 }
}
