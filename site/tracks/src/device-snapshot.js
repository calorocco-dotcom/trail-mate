import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';

export const RECORDING_FORMAT=Object.freeze({KML:0,GPX:1,CSV:2,BINARY:3});
const MAX_BYTES=8*1024*1024,CHUNK_BYTES=320;
const active=new WeakSet();
const cancelled=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('读取已取消','AbortError');};
const wait=(delay,signal)=>new Promise((resolve,reject)=>{
 const abort=()=>{clearTimeout(timer);reject(signal.reason??new DOMException('读取已取消','AbortError'));};
 const timer=setTimeout(()=>{signal?.removeEventListener('abort',abort);resolve();},delay);
 signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();
});

// Operation 81 reads a previously sealed snapshot, never a live recording path.
// Request: [snapshot_id16, offset, max_bytes, expected_sha25632|null].
// Response: [snapshot_id16, total_bytes, sha25632, format, chunk, offset].
export async function readDeviceTrackSnapshot(session,snapshotId,{signal,onProgress=()=>{},busyTimeoutMs=10000}={}){
 if(!session||typeof session.request!=='function'||!bytes(snapshotId,16)||!snapshotId.some(value=>value)||!Number.isFinite(busyTimeoutMs)||busyTimeoutMs<=0||busyTimeoutMs>60000)throw Error('设备轨迹读取参数无效');
 if(active.has(session))throw Error('当前设备已有轨迹读取，请等待或取消');
 cancelled(signal);const id=snapshotId.slice();active.add(session);
 try{
  let offset=0,total,sha256,format,source;
  do{
   cancelled(signal);const started=performance.now();let result;
   while(true){
    cancelled(signal);
    try{result=await session.request(OPERATION.TRACK_SNAPSHOT,[id.slice(),offset,CHUNK_BYTES,sha256?.slice()??null]);break;}
    catch(error){
     cancelled(signal);if(error.code!=='BUSY'&&error.code!==MANAGEMENT_STATUS.BUSY)throw error;
     const remaining=busyTimeoutMs-(performance.now()-started);if(remaining<=0)throw Error('设备轨迹读取持续忙碌，请稍后重试');
     await wait(Math.min(100,remaining),signal);
    }
   }
   cancelled(signal);
   if(!Array.isArray(result)||result.length!==6||!bytes(result[0],16)||!equal(result[0],id)||!integer(result[1],1,MAX_BYTES)||!bytes(result[2],32)||!integer(result[3],0,3)||!(result[4] instanceof Uint8Array)||!result[4].length||result[4].length>CHUNK_BYTES||result[5]!==offset)throw Error('设备轨迹快照响应无效');
   if(!source){total=result[1];sha256=result[2].slice();format=result[3];source=new Uint8Array(total);}
   if(total!==result[1]||format!==result[3]||!equal(sha256,result[2])||result[4].length>total-offset)throw Error('设备轨迹快照在读取期间发生变化');
   source.set(result[4],offset);offset+=result[4].length;
   onProgress(Object.freeze({offset,total}));
  }while(offset<total);
  cancelled(signal);const actual=new Uint8Array(await crypto.subtle.digest('SHA-256',source));cancelled(signal);
  if(!equal(actual,sha256))throw Object.assign(Error('设备轨迹快照哈希不匹配'),{code:'SNAPSHOT_HASH_MISMATCH'});
  return {snapshotId:id,source,sha256,format};
 }finally{active.delete(session);}
}
