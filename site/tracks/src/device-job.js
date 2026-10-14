import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {OPERATION} from '../../device/src/management-protocol.js';
const nonzero=(value,size)=>bytes(value,size)&&value.some(byte=>byte);
const phases=['accepted','uploading','file-stored','publishing','confirmed','failed','cancelled'];

// Query the original operation, never create a replacement after a lost reply.
export async function readTrackJob(session,binding,options={}){
  return requestTrackJob(session,binding,OPERATION.JOB_GET,options);
}
export async function requestTrackJob(session,binding,operation,{signal}={}){
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
  check();
  if(operation!==OPERATION.JOB_GET&&operation!==OPERATION.JOB_CANCEL)throw Error('轨迹任务操作无效');
  if(!session||typeof session.request!=='function'||!nonzero(binding?.operationId,16)||
    !nonzero(binding.objectId,32)||!nonzero(binding.revisionHash,32)||!nonzero(binding.directory,16)||
    !nonzero(binding.directoryEpoch,16)||!integer(binding.generation,1,0xffffffff)||!integer(binding.revision,1,0xffffffff))
    throw Error('轨迹任务查询绑定无效');
  const expected={...binding};
  for(const key of ['operationId','objectId','revisionHash','directory','directoryEpoch'])expected[key]=binding[key].slice();
  const retainedEvidence=['fileHash','fileBytes','authorHash'].some(key=>binding[key]!==undefined);
  if(retainedEvidence){
    if(!nonzero(binding.fileHash,32)||!nonzero(binding.authorHash,32)||!integer(binding.fileBytes,1,8388608))throw Error('轨迹任务查询文件或作者绑定无效');
    expected.fileHash=binding.fileHash.slice();expected.authorHash=binding.authorHash.slice();
  }
  const reply=await session.request(operation,[expected.operationId.slice(),expected.generation,1],{signal});
  check();
  const reject=()=>{throw Error('设备轨迹任务状态或绑定无效');};
  if(!Array.isArray(reply)||reply.length!==14)reject();
  for(const [index,key,size] of [[0,'operationId',16],[1,'objectId',32],[2,'revisionHash',32],[5,'directory',16],[6,'directoryEpoch',16]])
    if(!bytes(reply[index],size)||!equal(reply[index],expected[key]))reject();
  if(reply[3]!==expected.generation||reply[4]!==expected.revision||!nonzero(reply[7],32)||
    !integer(reply[8],1,8388608)||!integer(reply[9],0,reply[8])||!integer(reply[10],0,6)||
    !integer(reply[11],1,Number.MAX_SAFE_INTEGER)||!nonzero(reply[12],32)||!integer(reply[13],0,1)||
    reply[10]===0&&reply[9]!==0||[2,3,4].includes(reply[10])&&reply[9]!==reply[8])reject();
  if(retainedEvidence&&(!equal(reply[7],expected.fileHash)||reply[8]!==expected.fileBytes||!equal(reply[12],expected.authorHash)))reject();
  return {...expected,fileHash:reply[7].slice(),fileBytes:reply[8],offset:reply[9],phase:phases[reply[10]],
    sequence:reply[11],authorHash:reply[12].slice(),active:reply[13]===1,
    directoryConfirmed:reply[10]===4,published:reply[10]===4};
}
