import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
const encoder=new TextEncoder(),active=new WeakSet();
const cancelled=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('构建已取消','AbortError');};
const sleep=(ms,signal)=>new Promise((resolve,reject)=>{const abort=()=>{clearTimeout(timer);reject(signal.reason??new DOMException('构建已取消','AbortError'));};const timer=setTimeout(()=>{signal?.removeEventListener('abort',abort);resolve();},ms);signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();});
// Operation 86 starts or polls the same immutable selection. Capability must
// be checked by the UI before offering import. No ready result without seal.
export async function buildDeviceTrackSnapshot(session,entry,media,{signal,onProgress=()=>{},snapshotId=crypto.getRandomValues(new Uint8Array(16)),idleTimeoutMs=20000,pollIntervalMs=100}={}){
 const size=typeof entry?.bytes==='bigint'?entry.bytes:BigInt(Number.isSafeInteger(entry?.bytes)?entry.bytes:-1);
 const format={kml:0,gpx:1,csv:2,bin:3}[entry?.name?.split('.').at(-1).toLowerCase()];
 if(!session||typeof session.request!=='function'||!integer(media,0,0xffffffff)||!bytes(snapshotId,16)||!snapshotId.some(byte=>byte)||typeof entry?.name!=='string'||!entry.name||encoder.encode(entry.name).length>117||/[\x00-\x1f/\\:]/.test(entry.name)||entry.name.toLowerCase()==='active.bin'||entry.recording!==false||!integer(entry.format,0,3)||format!==entry.format||size<=0n||size>8388608n||!integer(idleTimeoutMs,1,60000)||!integer(pollIntervalMs,0,1000))throw Error('设备快照选择无效，请停止录制并刷新记录列表');
 if(active.has(session))throw Error('当前设备已有快照构建');
 cancelled(signal);active.add(session);
 const id=snapshotId.slice(),selection=Object.freeze({name:entry.name,bytes:Number(size),format:entry.format});let offset=0,lastProgress=performance.now(),ready=false,attempted=false;
 try{
  while(true){
   cancelled(signal);attempted=true;let result,busy=false;
   try{result=await session.request(OPERATION.TRACK_SNAPSHOT_BUILD,[id.slice(),media,selection.name,selection.bytes,selection.format]);}
   catch(error){if(error.code!==MANAGEMENT_STATUS.BUSY&&error.code!=='BUSY')throw error;busy=true;}
   cancelled(signal);
   if(!busy){
    if(!Array.isArray(result)||result.length!==5||!equal(result[0],id)||!integer(result[1],1,5)||!integer(result[2],offset,selection.bytes)||result[3]!==selection.bytes||result[4]!==selection.format)throw Error('设备快照构建状态无效');
    if(result[2]>offset){offset=result[2];lastProgress=performance.now();}
    onProgress(Object.freeze({offset,total:selection.bytes,state:result[1]}));
    if(result[1]===3){if(offset!==selection.bytes)throw Error('设备快照尚未完整封闭');ready=true;return {snapshotId:id,bytes:selection.bytes,format:selection.format};}
    if(result[1]!==1)throw Error(result[1]===4?'设备快照构建失败':'设备快照构建已取消');
   }
   if(performance.now()-lastProgress>=idleTimeoutMs)throw Error('设备快照构建持续无进展，请稍后重试');
   await sleep(pollIntervalMs,signal);
  }
 }finally{
  try{if(attempted&&!ready)await session.request(OPERATION.TRACK_SNAPSHOT_CANCEL,[id.slice()]);}catch{/* Device disconnect/expiry must also trigger scheduler cleanup. */}
  active.delete(session);
 }
}
