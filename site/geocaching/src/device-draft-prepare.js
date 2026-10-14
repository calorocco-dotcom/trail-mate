import {bytes,equal,integer} from './protocol.js';
import {decodeDeviceDraft,validateDraftPublication} from './device-draft.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const uint64=value=>typeof value==='bigint'?value>0n&&value<=0xffffffffffffffffn:Number.isSafeInteger(value)&&value>0;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

// Prepare only a committed device snapshot, never an unsaved editor projection.
// The caller retains prepareId across Busy and reconnect retries. No signing occurs here.
export async function prepareDeviceDraft(session,snapshot,directory,author,{prepareId,signal,onProgress=()=>{},
  timeoutMs=90000,retryMs=500,sleep=delay,now=()=>performance.now()}={}){
  if(!bytes(prepareId,16)||!prepareId.some(byte=>byte))throw Error('发布准备需要保留同一个随机准备 ID');
  if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Error('发布准备前必须验证设备作者身份');
  if(!directory?.capabilitiesVerified||!bytes(directory.delivery,16)||!bytes(directory.epoch,16))throw Error('发布准备需要选择已验证的设备目录');
  if(!bytes(snapshot?.draft?.id,16)||!uint64(snapshot.draft.generation)||!(snapshot.bytes instanceof Uint8Array)||
     !snapshot.bytes.length||snapshot.bytes.length>32768)throw Error('发布准备需要已提交的设备草稿快照');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('发布准备时间预算无效');
  const id=snapshot.draft.id.slice(),raw=snapshot.bytes.slice(),expected=snapshot.draft.generation;
  const draft=decodeDeviceDraft(id,raw),authorKey=author.publicKey.slice(),authorGeneration=author.generation;
  const target=directory.delivery.slice(),epoch=directory.epoch.slice(),token=prepareId.slice();
  if(BigInt(draft.generation)!==BigInt(expected))throw Error('草稿快照与设备代次不一致');
  if(draft.authorKey!==null&&!equal(draft.authorKey,authorKey))throw Error('当前设备不能发布其他作者的草稿');
  validateDraftPublication(draft);
  const deadline=now()+timeoutMs;
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('发布准备已取消','AbortError');
    if(now()>=deadline)throw Error('发布准备尚未确认，请保留相同准备 ID 并检查设备连接');};
  check();
  const contentHash=new Uint8Array(await crypto.subtle.digest('SHA-256',raw));
  check();onProgress({phase:'checking',prepareId:token.slice()});
  for(;;){
    check();let reply;const sentAt=now();
    try{reply=await session.request(OPERATION.CACHE_PREPARE,[id,expected,target,authorGeneration,token]);}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;
      await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(reply)||reply.length!==10)throw Error('设备发布准备回执无效');
    const [confirmedToken,confirmedId,generation,confirmedAuthor,confirmedTarget,confirmedEpoch,confirmedHash,from,to,remaining]=reply;
    if(!bytes(confirmedToken,16)||!equal(confirmedToken,token)||!bytes(confirmedId,16)||!equal(confirmedId,id)||
       !uint64(generation)||BigInt(generation)!==BigInt(expected)||!bytes(confirmedAuthor,64)||!equal(confirmedAuthor,authorKey)||
       !bytes(confirmedTarget,16)||!equal(confirmedTarget,target)||!bytes(confirmedEpoch,16)||!equal(confirmedEpoch,epoch)||
       !bytes(confirmedHash,32)||!equal(confirmedHash,contentHash)||!integer(from,0,0xffffffff)||!integer(to,1,0xffffffff)||
       !(to===from||(from!==0xffffffff&&to===from+1))||!integer(remaining,1,120000)||
       (from>0&&draft.baseHash===null))throw Error('设备发布准备绑定与当前草稿、作者或目录不一致');
    // Remaining time was measured on the device before reception. Starting at
    // send time avoids overstating validity by the USB/request round trip.
    const expiresAt=sentAt+remaining;
    if(expiresAt<=now())throw Error('发布准备已过期，请重新检查设备与目录');
    check();onProgress({phase:'prepared',prepareId:token.slice(),expiresAt});
    check();if(expiresAt<=now())throw Error('发布准备已过期，请重新检查设备与目录');
    return {prepareId:token,id,generation,authorKey,authorGeneration,directory:target,directoryEpoch:epoch,
      contentHash,fromRevision:from,toRevision:to,remainingMs:Math.max(0,expiresAt-now()),expiresAt};
  }
}
