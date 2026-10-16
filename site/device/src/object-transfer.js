import {OPERATION,MANAGEMENT_STATUS} from './management-protocol.js';
import {equal, bytes, integer} from '../../geocaching/src/protocol.js';

const fail = message => { throw new Error(message); };
function status(value,id,total){
 if(!Array.isArray(value)||value.length!==4||!bytes(value[0],16)||!equal(value[0],id)||
    !integer(value[1],1,384)||!integer(value[2],0,total)||!integer(value[3],0,1))fail('Invalid device transfer status');
 if(value[3]===1&&value[2]!==total)fail('Device sealed an incomplete object');
 return {chunkBytes:value[1],offset:value[2],sealed:value[3]===1};
}

// Retains the source only in the browser. Sequential chunks obey the device's
// negotiated budget. A disconnect leaves the durable transfer for later resume.
export async function uploadObject(session,source,{id,kind,sha256,signal,onProgress=()=>{},beginTimeoutMs=30000,beginPollMs=100,finishTimeoutMs=900000,finishPollMs=100}){
 if(!(source instanceof Uint8Array)||!source.length||source.length>0xffffffff||
    !bytes(id,16)||!bytes(sha256,32)||!integer(kind,0,65535)||!integer(beginTimeoutMs,1,1800000)||!integer(beginPollMs,0,1000)||!integer(finishTimeoutMs,1,1800000)||!integer(finishPollMs,0,1000))fail('Invalid upload object');
 const checkAbort=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Upload paused','AbortError');};
 checkAbort();
 const transferId=id.slice(),expectedHash=sha256.slice(),content=source.slice();
 const beginDeadline=performance.now()+beginTimeoutMs;
 let current;
 while(!current){
  checkAbort();
  try{current=status(await session.request(OPERATION.TRANSFER_BEGIN,[transferId.slice(),kind,content.length,expectedHash.slice()]),transferId,content.length);}
  catch(error){
   // A received Busy did not admit this transfer. Keep exactly the same
   // binding while storage reconciles a prior completed intent. A lost reply,
   // conflict or changed identity is never an automatic retry.
   if(error.code!==MANAGEMENT_STATUS.BUSY&&error.code!=='BUSY')throw error;
   checkAbort();const remaining=beginDeadline-performance.now();if(remaining<=0)fail('Device upload admission timed out; binding retained for resume');
   await pauseUpload(Math.min(beginPollMs,remaining),signal);
  }
 }
 checkAbort();
 onProgress({offset:current.offset,total:content.length,sealed:current.sealed});
 while(!current.sealed&&current.offset<content.length){
   checkAbort();
   const end=Math.min(content.length,current.offset+current.chunkBytes),chunk=content.slice(current.offset,end);
   const next=status(await session.request(OPERATION.TRANSFER_CHUNK,[transferId.slice(),current.offset,chunk]),transferId,content.length);
   checkAbort();
   if(next.offset!==end||next.sealed)fail('Unexpected device upload acknowledgement');
   current=next;onProgress({offset:current.offset,total:content.length,sealed:false});
 }
 if(!current.sealed){
  const deadline=performance.now()+finishTimeoutMs;
  onProgress({offset:current.offset,total:content.length,sealed:false,stage:'validating'});
  while(!current.sealed){
   checkAbort();let busy=false,result;
   try{result=await session.request(OPERATION.TRANSFER_FINISH,[transferId.slice()]);}catch(error){if(error.code!==MANAGEMENT_STATUS.BUSY&&error.code!=='BUSY')throw error;busy=true;}
   checkAbort();
   if(!busy){current=status(result,transferId,content.length);if(current.offset!==content.length)fail('Device validation changed durable offset');}
   if(current.sealed)break;
   const remaining=deadline-performance.now();if(remaining<=0)fail('Device object validation timed out; transfer retained for resume');
   await pauseUpload(Math.min(finishPollMs,remaining),signal);
  }
 }
 if(!current.sealed||current.offset!==content.length)fail('Device did not validate and seal the object');
 onProgress({offset:current.offset,total:content.length,sealed:true});
 return {id:transferId.slice(),sha256:expectedHash.slice(),bytes:content.length};
}

function pauseUpload(ms,signal){
 return new Promise((resolve,reject)=>{const abort=()=>{clearTimeout(timer);signal?.removeEventListener('abort',abort);reject(signal.reason??new DOMException('Upload paused','AbortError'));};const timer=setTimeout(()=>{signal?.removeEventListener('abort',abort);resolve();},ms);signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();});
}
