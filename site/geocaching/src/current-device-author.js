import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {validateAuthorInfo} from '../../device/src/author-identity.js';
import {bytes,equal,integer} from './protocol.js';
// Retry only explicit Busy on this read, keeping the exact expected identity.
// No signature/confirmation admission is repeated and no lease is released.
export async function readCurrentDeviceAuthor(session,author,{signal,timeoutMs=5000,retryMs=100,
 sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms)),now=()=>performance.now()}={}){
 if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Object.assign(Error('Device author changed'),{code:'STALE_IDENTITY'});
 const publicKey=author.publicKey.slice(),generation=author.generation,deadline=now()+timeoutMs;
 if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('Invalid author read budget');
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};
 for(;;){
  check();let response;
  try{response=await session.request(OPERATION.AUTHOR_INFO,[]);}
  catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY||now()>=deadline)throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
  check();const current=await validateAuthorInfo(response);check();
  if(!equal(current.publicKey,publicKey)||current.generation!==generation)throw Object.assign(Error('Device author changed'),{code:'STALE_IDENTITY'});
  return current;
 }
}
