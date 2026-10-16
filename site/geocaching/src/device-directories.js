import {Identity} from '@reticulum/core';
import {bytes,equal,integer,validText} from './protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const uint64=value=>typeof value==='bigint'?value>=0n&&value<=0xffffffffffffffffn:Number.isSafeInteger(value)&&value>=0;
const utf8=new TextEncoder(),delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function decodeDeviceDirectory(value){
  if(!Array.isArray(value)||value.length!==9)throw Error('设备目录信息格式无效');
  const [delivery,discovery,publicKey,epoch,sequence,name,flags,maxQueryItems,ageMs]=value;
  if(!bytes(delivery,16)||!bytes(discovery,16)||!bytes(publicKey,64)||!bytes(epoch,16)||!uint64(sequence)||
     !validText(name,40)||!integer(flags,0,3)||!integer(maxQueryItems,0,255)||!uint64(ageMs)||((flags&1)&&!maxQueryItems))throw Error('设备目录信息格式无效');
  const identity=await Identity.fromPublicKey(publicKey);
  for(const [aspect,destination] of [['lxmf.delivery',delivery],['trailmate.geocache.directory',discovery]]){
    const prefix=(await Identity.fullHash(utf8.encode(aspect))).slice(0,10);
    const expected=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash]));
    if(!equal(expected,destination))throw Error('目录公钥与 Reticulum 地址不一致');
  }
  return {delivery:delivery.slice(),discovery:discovery.slice(),publicKey:publicKey.slice(),epoch:epoch.slice(),sequence,name,
    capabilitiesVerified:Boolean(flags&1),inFlight:Boolean(flags&2),maxQueryItems,ageMs};
}
export async function readDeviceDirectories(session,{signal,waitForReadyTarget=false,timeoutMs=30000,retryMs=100,sleep=delay,now=()=>Date.now()}={}){
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('目录读取时间预算无效');
  const deadline=now()+timeoutMs,rows=[],seen=new Set();
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('目录读取已取消','AbortError');if(now()>=deadline)throw Error('设备目录发现尚未就绪，请检查 Reticulum 通道后刷新');};
  for(let offset=0;offset<=4;){
    check();let reply;
    try{reply=await session.request(OPERATION.CACHE_DIRECTORY_LIST,[offset]);}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(reply)||reply.length!==3||reply[0]!==offset||!integer(reply[1],0,4))throw Error('设备目录列表响应无效');
    if(reply[2]===null){
      if(offset<reply[1])throw Error('设备目录结束标记无效');
      if(waitForReadyTarget&&!rows.some(row=>row.capabilitiesVerified)){
        rows.length=0;seen.clear();offset=0;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;
      }
      return rows;
    }
    if(offset>=reply[1]||offset===4)throw Error('设备目录列表超出容量');
    const row=await decodeDeviceDirectory(reply[2]);check();
    const id=Array.from(row.delivery,b=>b.toString(16).padStart(2,'0')).join('');
    if(seen.has(id))throw Error('设备目录列表包含重复地址');seen.add(id);rows.push(row);offset++;
  }
  throw Error('设备目录列表未结束');
}
