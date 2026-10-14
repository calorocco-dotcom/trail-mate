import {Identity,toHex} from '@reticulum/core';
import {bytes,equal,integer,encode} from '../../geocaching/src/protocol.js';
import {OPERATION} from './management-protocol.js';

const utf8=new TextEncoder();
const domain=utf8.encode('trailmate.hostlink.author/v1\0');
const join=(...parts)=>{const output=new Uint8Array(parts.reduce((size,part)=>size+part.length,0));let offset=0;for(const part of parts){output.set(part,offset);offset+=part.length;}return output;};

// Fixed purpose proof. This format cannot ask the device to sign arbitrary
// webpage content or obtain private key bytes. It binds both halves of the
// combined public key, a device identity generation and this USB session.
export function authorProofMessage(sessionId,challenge,publicKey,generation){
  if(!bytes(sessionId,16)||!bytes(challenge,32)||!bytes(publicKey,64)||!integer(generation,1,0xffffffff))throw Error('Invalid device author proof fields');
  return join(domain,encode([1,sessionId,challenge,publicKey,generation]));
}

export async function validateAuthorInfo(value){
  if(!Array.isArray(value)||value.length!==4||!bytes(value[0],64)||!bytes(value[1],16)||!bytes(value[2],16)||!integer(value[3],1,0xffffffff))throw Error('设备作者信息格式无效');
  const [publicKey,identityHash,destination,generation]=value;
  const identity=await Identity.fromPublicKey(publicKey);
  const nameHash=(await Identity.fullHash(utf8.encode('lxmf.delivery'))).slice(0,10);
  const expectedDestination=await Identity.truncatedHash(join(nameHash,identity.identityHash));
  if(!equal(identity.identityHash,identityHash)||!equal(expectedDestination,destination))throw Error('设备作者公钥、身份哈希与 LXMF 地址不一致');
  return {publicKey:publicKey.slice(),identityHash:identityHash.slice(),destination:destination.slice(),generation,
    fingerprint:toHex(await Identity.fullHash(publicKey))};
}

export async function proveDeviceAuthor(session,{randomBytes=size=>crypto.getRandomValues(new Uint8Array(size)),signal}={}){
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Author verification paused','AbortError');};
  check();
  const sessionId=session.id;
  if(!bytes(sessionId,16))throw Error('设备管理会话标识无效');
  const info=await validateAuthorInfo(await session.request(OPERATION.AUTHOR_INFO,[]));
  check();
  const challenge=randomBytes(32);
  if(!bytes(challenge,32))throw Error('设备作者挑战随机数无效');
  const response=await session.request(OPERATION.AUTHOR_PROVE,[challenge,info.generation]);
  check();
  if(!Array.isArray(response)||response.length!==5||!bytes(response[4],64))throw Error('设备作者证明格式无效');
  const proofInfo=await validateAuthorInfo(response.slice(0,4));
  check();
  if(!equal(info.publicKey,proofInfo.publicKey)||info.generation!==proofInfo.generation)throw Error('设备作者身份在验证期间发生变化');
  const identity=await Identity.fromPublicKey(info.publicKey);
  if(!await identity.validate(response[4],authorProofMessage(sessionId,challenge,info.publicKey,info.generation)))throw Error('设备作者证明验签失败');
  check();
  return {...info,verified:true};
}
