import {bytes,equal,integer} from './protocol.js';
import {encodeDraftEdit} from './device-draft.js';
import {uploadObject} from '../../device/src/object-transfer.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

export async function saveDeviceDraft(session,id,fields,expected,author,{transferId,signal,onProgress=()=>{},
  timeoutMs=90000,retryMs=100,sleep=delay,now=()=>Date.now()}={}){
  if(!bytes(transferId,16))throw Error('保存草稿需要保留同一个传输 ID');
  if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Error('保存草稿前必须验证设备作者身份');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('草稿保存时间预算无效');
  const source=encodeDraftEdit(id,fields,expected),key=id.slice(),operation=transferId.slice(),authorGeneration=author.generation;
  const deadline=now()+timeoutMs;
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('草稿保存已暂停，结果仍需确认','AbortError');if(now()>=deadline)throw Error('草稿保存结果未确认，请使用相同内容与传输 ID 重试');};
  check();
  const sha256=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
  check();
  await uploadObject(session,source,{id:operation,kind:2,sha256,signal,onProgress:p=>{check();onProgress({...p,phase:p.sealed?'sealed':'uploading'});}});
  check();onProgress({offset:source.length,total:source.length,phase:'committing'});
  for(;;){
    check();let reply;
    try{reply=await session.request(OPERATION.CACHE_DRAFT_PUT,[operation,key,expected,authorGeneration]);}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(reply)||reply.length!==3||!bytes(reply[0],16)||!equal(reply[0],operation)||
       !bytes(reply[1],16)||!equal(reply[1],key)||
       !(typeof reply[2]==='bigint'||Number.isSafeInteger(reply[2]))||BigInt(reply[2])!==BigInt(expected)+1n)throw Error('设备草稿保存回执无效，结果仍需确认');
    onProgress({offset:source.length,total:source.length,phase:'saved'});
    return {id:key,transferId:operation,generation:reply[2],sha256,source};
  }
}
