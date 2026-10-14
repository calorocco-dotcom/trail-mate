import {Identity} from '@reticulum/core';
import {bytes,equal,integer,validText} from '../../geocaching/src/protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const utf8=new TextEncoder(),delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const nonzero=value=>value.some(byte=>byte!==0);
async function decodeDirectory(row){
  if(!Array.isArray(row)||row.length!==8)throw Error('轨迹目录信息格式无效');
  const [rawDelivery,rawPublic,rawEpoch,name,ready,fileLimit,chunkLimit,queryLimit]=row;
  if(!bytes(rawDelivery,16)||!bytes(rawPublic,64)||!bytes(rawEpoch,16)||!nonzero(rawDelivery)||!nonzero(rawEpoch)||
    !validText(name,40)||!name.trim()||!integer(ready,0,1)||!integer(fileLimit,0,8388608)||!integer(chunkLimit,0,1024)||!integer(queryLimit,0,32)||
    (ready?!fileLimit||!chunkLimit||!queryLimit:fileLimit!==0||chunkLimit!==0||queryLimit!==0))throw Error('轨迹目录信息格式无效');
  const delivery=rawDelivery.slice(),publicKey=rawPublic.slice(),epoch=rawEpoch.slice();
  const identity=await Identity.fromPublicKey(publicKey);
  const prefix=(await Identity.fullHash(utf8.encode('lxmf.delivery'))).slice(0,10);
  const expected=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash]));
  if(!equal(expected,delivery))throw Object.assign(Error('轨迹目录公钥与 Reticulum 地址不一致'),{code:'DIRECTORY_KEY_MISMATCH'});
  return Object.freeze({protocol:'trailmate.track.v1',delivery,publicKey,epoch,name,capabilitiesVerified:ready===1,fileLimit,chunkLimit,queryLimit});
}
export async function readTrackDirectories(session,{signal,timeoutMs=30000,retryMs=100,sleep=delay,now=()=>Date.now()}={}){
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('轨迹目录读取时间预算无效');
  const deadline=now()+timeoutMs,rows=[],seen=new Set();
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('轨迹目录读取已取消','AbortError');if(now()>=deadline)throw Error('轨迹目录发现尚未就绪，请检查 Reticulum 通道后刷新');};
  for(let cursor=0;cursor<8;){
    check();let reply;
    try{reply=await session.request(OPERATION.TRACK_DIRECTORY_LIST,[cursor]);}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY&&error.code!=='BUSY')throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(reply)||reply.length!==2||!Array.isArray(reply[1])||reply[1].length>1||
      (reply[0]!==null&&(!integer(reply[0],1,7)||reply[0]!==cursor+1||reply[1].length!==1)))throw Error('轨迹目录分页响应无效');
    if(reply[1].length){
      const row=await decodeDirectory(reply[1][0]);check();
      const id=Array.from(row.delivery,byte=>byte.toString(16).padStart(2,'0')).join('');
      if(seen.has(id))throw Error('轨迹目录列表包含重复地址');seen.add(id);rows.push(row);
    }
    if(reply[0]===null)return Object.freeze(rows);
    cursor=reply[0];
  }
  throw Error('轨迹目录列表未结束');
}
