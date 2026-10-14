import {Identity} from '@reticulum/core';
import {bytes,equal,integer,encode,decode} from '../../geocaching/src/protocol.js';
const utf8=new TextEncoder(),domain=utf8.encode('TrailMate.Identity.Backup.v1\0');
const fail=code=>{throw Object.assign(Error(code),{code});};
const concat=(...parts)=>{const out=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let offset=0;for(const part of parts){out.set(part,offset);offset+=part.length;}return out;};
export const IDENTITY_KEY_FORMAT=Object.freeze({ARDUINO_EXPANDED:0,RETICULUM_SEED:1});
export function encodeIdentityBackupPayload({format,publicKey,privateKey,identityHash,delivery,generation}){
 const size=format===0?96:format===1?64:0;
 if(!size||!bytes(publicKey,64)||!publicKey.some(Boolean)||!bytes(privateKey,size)||!privateKey.slice(0,32).some(Boolean)||!privateKey.slice(32).some(Boolean)||!bytes(identityHash,16)||!bytes(delivery,16)||!integer(generation,1,0xffffffff))fail('IDENTITY_BACKUP_PAYLOAD_INVALID');
 return encode([1,'trailmate.identity.backup.v1',format,publicKey,privateKey,identityHash,delivery,generation]);
}
export function identityBackupSigningBytes(payload){
 const decoded=decodeIdentityBackupPayload(payload);decoded.privateKey.fill(0);return concat(domain,payload);
}
export function decodeIdentityBackupPayload(payload){
 if(!(payload instanceof Uint8Array)||payload.length>320)fail('IDENTITY_BACKUP_PAYLOAD_INVALID');
 const row=decode(payload,320);
 if(!Array.isArray(row)||row.length!==8||row[0]!==1||row[1]!=='trailmate.identity.backup.v1')fail('IDENTITY_BACKUP_PAYLOAD_INVALID');
 const result={format:row[2],publicKey:row[3],privateKey:row[4],identityHash:row[5],delivery:row[6],generation:row[7]};encodeIdentityBackupPayload(result);
 return {...result,publicKey:result.publicKey.slice(),privateKey:result.privateKey.slice(),identityHash:result.identityHash.slice(),delivery:result.delivery.slice()};
}
// Verify a device-authenticated export before encryption or restoration. The
// signature binds the private representation too; native restore must still
// validate its public/private consistency with the platform crypto backend.
export async function verifyIdentityBackupPayload(signed,expectedAuthor){
 if(!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64)||!expectedAuthor?.verified||!bytes(expectedAuthor.publicKey,64)||!integer(expectedAuthor.generation,1,0xffffffff))fail('IDENTITY_BACKUP_AUTHOR_INVALID');
 const payload=signed[0]?.slice(),signature=signed[1].slice(),expected=expectedAuthor.publicKey.slice(),generation=expectedAuthor.generation;
 let result,message,accepted=false;
 try{
  result=decodeIdentityBackupPayload(payload);
  if(!equal(expected,result.publicKey)||generation!==result.generation)fail('IDENTITY_BACKUP_AUTHOR_MISMATCH');
  const identity=await Identity.fromPublicKey(result.publicKey);
  const prefix=(await Identity.fullHash(utf8.encode('lxmf.delivery'))).slice(0,10),delivery=await Identity.truncatedHash(concat(prefix,identity.identityHash));
  if(!equal(identity.identityHash,result.identityHash)||!equal(delivery,result.delivery))fail('IDENTITY_BACKUP_ADDRESS_MISMATCH');
  message=identityBackupSigningBytes(payload);
  if(!await identity.validate(signature,message))fail('IDENTITY_BACKUP_SIGNATURE_INVALID');
  accepted=true;return result;
 }finally{payload.fill(0);signature.fill(0);message?.fill(0);if(!accepted)result?.privateKey.fill(0);}
}
