import {bytes,equal,integer} from './protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {decodeDeviceDraft} from './device-draft.js';

const max64=0xffffffffffffffffn;
const validGeneration=value=>typeof value==='bigint'?value>0n&&value<=max64:Number.isSafeInteger(value)&&value>0;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

// Device generation and draft generation are separate. Each continuation
// binds both, and Busy is a bounded poll rather than permission to start again.
export async function readDeviceDraft(session,id,author,{signal,onProgress=()=>{},timeoutMs=30000,retryMs=100,sleep=delay,now=()=>Date.now()}={}){
  if(!bytes(id,16)||!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Error('读取草稿前必须验证连接设备的作者身份');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('草稿读取时间预算无效');
  // Freeze correlation inputs before the first await. A reconnect or identity
  // refresh may replace the caller's state while this read is in flight.
  id=id.slice();
  const authorKey=author.publicKey.slice(),authorGeneration=author.generation;
  const deadline=now()+timeoutMs;
  const checkActive=()=>{
    if(signal?.aborted)throw signal.reason??new DOMException('草稿读取已取消','AbortError');
    if(now()>=deadline)throw Error('草稿读取超时，未替换当前编辑内容');
  };
  let offset=0,total,generation=0,output;
  while(total===undefined||offset<total){
    checkActive();
    let reply;
    try{reply=await session.request(OPERATION.CACHE_DRAFT_GET,[id,offset,320,generation,authorGeneration]);}
    catch(error){checkActive();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    checkActive();
    if(!Array.isArray(reply)||reply.length!==5||!bytes(reply[0],16)||!equal(reply[0],id)||!validGeneration(reply[1])||
       !integer(reply[2],1,32768)||!integer(reply[3],0,32768)||reply[3]!==offset||
       !(reply[4] instanceof Uint8Array)||!reply[4].length||reply[4].length>320||offset+reply[4].length>reply[2])throw Error('设备草稿分块响应无效');
    if(total===undefined){total=reply[2];generation=reply[1];output=new Uint8Array(total);}
    else if(total!==reply[2]||BigInt(generation)!==BigInt(reply[1]))throw Error('设备草稿在读取期间发生变化，请重新读取');
    output.set(reply[4],offset);offset+=reply[4].length;onProgress({offset,total});
  }
  checkActive();
  const draft=decodeDeviceDraft(id,output);
  if(BigInt(draft.generation)!==BigInt(generation))throw Error('草稿内容与设备代次不一致');
  return {draft,bytes:output,editable:draft.authorKey===null||equal(draft.authorKey,authorKey)};
}
