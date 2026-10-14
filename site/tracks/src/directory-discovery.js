import {bytes} from '../../geocaching/src/protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
export function parseDirectoryDiscoveryAddress(value){
  if(typeof value!=='string')throw Object.assign(Error('请输入目录发现地址'),{code:'DIRECTORY_ADDRESS_INVALID'});
  let text=value.trim();if(/^<[0-9a-fA-F]{32}>$/.test(text))text=text.slice(1,-1);
  if(!/^[0-9a-fA-F]{32}$/.test(text))throw Object.assign(Error('目录发现地址须为 32 个十六进制字符'),{code:'DIRECTORY_ADDRESS_INVALID'});
  const result=Uint8Array.from(text.match(/../g),pair=>parseInt(pair,16));
  if(!result.some(byte=>byte))throw Object.assign(Error('目录发现地址不能全为零'),{code:'DIRECTORY_ADDRESS_ZERO'});
  return result;
}
export async function requestTrackDirectoryDiscovery(session,destination,{signal,timeoutMs=30000,retryMs=1000,sleep=delay,now=()=>Date.now()}={}){
  if(!bytes(destination,16)||!destination.some(byte=>byte))throw Error('目录发现地址无效');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('目录查找时间预算无效');
  const target=destination.slice(),deadline=now()+timeoutMs;
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('目录查找已取消','AbortError');if(now()>=deadline)throw Error('目录请求尚未就绪，请检查设备网络或运行状态');};
  while(true){
    check();let response;
    try{response=await session.request(OPERATION.TRACK_DIRECTORY_DISCOVER,[target.slice()]);}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY&&error.code!=='BUSY')throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(response)||response.length)throw Error('目录查找响应无效');
    return Object.freeze({requested:true,discovery:target});
  }
}
