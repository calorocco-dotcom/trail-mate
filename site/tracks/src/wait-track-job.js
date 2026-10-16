import {readTrackJob} from './device-job.js';
import {equal} from '../../geocaching/src/protocol.js';
import {MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const copy=value=>({...value,...Object.fromEntries(Object.entries(value).filter(([,v])=>v instanceof Uint8Array).map(([k,v])=>[k,v.slice()]))});
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

// Pausing this browser wait never sends JobCancel or another TrackCommit.
export async function waitTrackJob(session,binding,{signal,timeoutMs=120000,pollMs=250,
  sleep=delay,now=()=>performance.now(),onProgress=()=>{}}={}){
  if(!binding||!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(pollMs)||pollMs<=0||
    typeof sleep!=='function'||typeof now!=='function'||typeof onProgress!=='function')throw Error('轨迹任务等待参数无效');
  const expected=copy(binding),deadline=now()+timeoutMs;
  let previous=null;
  const pending=(cause,paused=false)=>{
    const error=Error(paused?'已暂停网页等待；设备任务没有取消。':'任务结果尚未确认，请用原操作 ID 继续查询。',{cause});
    error.code=paused?'JOB_WAIT_PAUSED':'JOB_PENDING';error.operationId=expected.operationId?.slice();
    error.lastJob=previous?copy(previous):null;return error;
  };
  const check=()=>{if(signal?.aborted)throw pending(signal.reason,true);if(now()>=deadline)throw pending();};
  for(;;){
    check();let job;
    try{job=await readTrackJob(session,expected,{signal});}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw pending(error);
      await sleep(Math.min(pollMs,Math.max(1,deadline-now())));continue;}
    check();
    if(previous && (job.sequence<previous.sequence || job.offset<previous.offset || job.fileBytes!==previous.fileBytes ||
      !equal(job.fileHash,previous.fileHash)||!equal(job.authorHash,previous.authorHash)||
      job.sequence===previous.sequence&&(job.offset!==previous.offset||job.phase!==previous.phase)||
      ['accepted','uploading','file-stored','publishing','confirmed'].indexOf(job.phase)>=0&&
      ['accepted','uploading','file-stored','publishing','confirmed'].indexOf(job.phase)<
      ['accepted','uploading','file-stored','publishing','confirmed'].indexOf(previous.phase)))
      throw pending(Error('设备任务进度回退或不可变绑定变化'));
    previous=copy(job);onProgress(copy(job));
    if(job.directoryConfirmed||['failed','cancelled'].includes(job.phase)||!job.active)return copy(previous);
    await sleep(Math.min(pollMs,Math.max(1,deadline-now())));
  }
}
