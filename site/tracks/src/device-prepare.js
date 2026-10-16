import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';

const nonzero=(value,size)=>bytes(value,size)&&value.some(byte=>byte);
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

// Retain operationId and expected bindings across retries. Prepared means the
// device persisted a signed revision; directory publication is a later step.
export async function prepareTrack(session,{operationId,kmlUploadId,manifestUploadId,generation,parentHash=null,
  objectId,revisionHash,revision},{signal,onProgress=()=>{},timeoutMs=90000,retryMs=500,
  sleep=delay,now=()=>performance.now()}={}){
  if(!session||typeof session.request!=='function'||!nonzero(operationId,16)||!nonzero(kmlUploadId,16)||
     !nonzero(manifestUploadId,16)||equal(kmlUploadId,manifestUploadId)||!integer(generation,1,0xffffffff)||
     parentHash!==null&&!nonzero(parentHash,32)||!nonzero(objectId,32)||!nonzero(revisionHash,32)||
     !integer(revision,1,0xffffffff)||(revision===1)!==(parentHash===null))throw Error('轨迹准备请求或版本绑定无效');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0||retryMs>=30000||
     typeof sleep!=='function'||typeof now!=='function'||typeof onProgress!=='function')throw Error('轨迹准备时间预算无效');
  const token=operationId.slice(),kml=kmlUploadId.slice(),manifest=manifestUploadId.slice(),parent=parentHash?.slice()??null;
  const expectedObject=objectId.slice(),expectedHash=revisionHash.slice(),deadline=now()+timeoutMs;
  const check=()=>{
    if(signal?.aborted)throw signal.reason??new DOMException('轨迹准备已暂停','AbortError');
    if(now()>=deadline)throw Error('轨迹准备尚未确认，请保留相同操作 ID 后重试');
  };
  check();onProgress({phase:'checking',operationId:token.slice()});
  for(;;){
    check();let reply,busy=false;
    try{reply=await session.request(OPERATION.TRACK_PREPARE,[token.slice(),kml.slice(),manifest.slice(),generation,parent?.slice()??null]);}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;busy=true;}
    check();
    if(!busy){
      if(!Array.isArray(reply)||reply.length!==5||!bytes(reply[0],16)||!equal(reply[0],token)||!integer(reply[1],0,1))throw Error('设备轨迹准备回执无效');
      if(reply[1]===1){
        if(!bytes(reply[2],32)||!equal(reply[2],expectedObject)||!bytes(reply[3],32)||!equal(reply[3],expectedHash)||
           reply[4]!==revision)throw Error('设备轨迹准备结果与当前版本绑定不一致');
        onProgress({phase:'prepared',operationId:token.slice()});check();
        return {operationId:token,objectId:expectedObject,revisionHash:expectedHash,revision,generation,prepared:true,published:false};
      }
      if(reply[2]!==null||reply[3]!==null||reply[4]!==0)throw Error('设备轨迹准备进度回执无效');
    }
    await sleep(Math.min(retryMs,Math.max(1,deadline-now())));
  }
}
