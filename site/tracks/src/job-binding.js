import {encode,decode,bytes,integer,equal} from '../../geocaching/src/protocol.js';
const nonzero=(value,size)=>bytes(value,size)&&value.some(byte=>byte);
// Persist before TrackCommit. This records intent, never publication success.
export function encodeTrackJobBinding(binding){
  if(!binding||!nonzero(binding.operationId,16)||!nonzero(binding.objectId,32)||!nonzero(binding.revisionHash,32)||
    !nonzero(binding.directory,16)||!nonzero(binding.directoryEpoch,16)||!integer(binding.generation,1,0xffffffff)||
    !integer(binding.revision,1,0xffffffff)||!nonzero(binding.authorHash,32)||!nonzero(binding.fileHash,32)||
    !integer(binding.fileBytes,1,8388608))throw Error('轨迹任务保存绑定无效');
  return encode([1,binding.operationId,binding.objectId,binding.revisionHash,binding.generation,binding.revision,
    binding.directory,binding.directoryEpoch,binding.authorHash,binding.fileHash,binding.fileBytes]);
}
export function decodeTrackJobBinding(raw){
  if(!(raw instanceof Uint8Array)||raw.length>512)throw Error('轨迹任务保存记录无效');
  const value=decode(raw,512);
  if(!Array.isArray(value)||value.length!==11||value[0]!==1)throw Error('轨迹任务保存记录版本无效');
  const binding={operationId:value[1],objectId:value[2],revisionHash:value[3],generation:value[4],revision:value[5],
    directory:value[6],directoryEpoch:value[7],authorHash:value[8],fileHash:value[9],fileBytes:value[10]};
  const canonical=encodeTrackJobBinding(binding);
  if(!equal(canonical,raw))throw Error('轨迹任务保存记录编码不一致');
  for(const key of ['operationId','objectId','revisionHash','directory','directoryEpoch','authorHash','fileHash'])binding[key]=binding[key].slice();
  return binding;
}
