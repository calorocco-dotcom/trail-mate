import {encode,decode,bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {validateTrackManifest} from './manifest-schema.js';

// Compact durable retry descriptor. Store with the draft's compare-and-swap
// version; the KML remains in the existing draft file store, never duplicated.
export function encodeTrackPublicationPlan({operationId,kmlUploadId,manifestUploadId,generation,manifest,fileHash,fileBytes}){
  if(![operationId,kmlUploadId,manifestUploadId].every(id=>bytes(id,16)&&id.some(v=>v))||equal(kmlUploadId,manifestUploadId)||
     !integer(generation,1,0xffffffff)||!(manifest instanceof Uint8Array)||!manifest.length||manifest.length>4096||
     !bytes(fileHash,32)||!integer(fileBytes,1,8388608))throw Error('轨迹重试计划字段无效');
  const record=validateTrackManifest(decode(manifest,4096));
  if(!equal(record[13][0],fileHash)||record[13][1]!==fileBytes)throw Error('轨迹重试计划与文件绑定不一致');
  const raw=encode([1,operationId,kmlUploadId,manifestUploadId,generation,manifest,fileHash,fileBytes]);
  if(raw.length>4352)throw Error('轨迹重试计划超过容量限制');
  return raw;
}
export function decodeTrackPublicationPlan(source){
  const row=decode(source,4352);
  if(!Array.isArray(row)||row.length!==8||row[0]!==1)throw Error('轨迹重试计划格式无效');
  const plan={operationId:row[1],kmlUploadId:row[2],manifestUploadId:row[3],generation:row[4],manifest:row[5],fileHash:row[6],fileBytes:row[7]};
  encodeTrackPublicationPlan(plan);
  return {...plan,operationId:plan.operationId.slice(),kmlUploadId:plan.kmlUploadId.slice(),manifestUploadId:plan.manifestUploadId.slice(),
    manifest:plan.manifest.slice(),fileHash:plan.fileHash.slice()};
}
export function validateTrackPublicationResume(plan,author,fileHash,fileBytes){
  const retained=decodeTrackPublicationPlan(encodeTrackPublicationPlan(plan)),record=decode(retained.manifest,4096);
  if(!author?.verified||!bytes(author.publicKey,64)||!equal(author.publicKey,record[1])||author.generation!==retained.generation)
    throw Error('设备身份已变化，不能继续原轨迹准备');
  if(!bytes(fileHash,32)||!equal(fileHash,retained.fileHash)||fileBytes!==retained.fileBytes)
    throw Error('轨迹草稿已变化，不能复用原准备操作 ID');
  return retained;
}
