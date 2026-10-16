import {encode,decode,bytes} from '../../geocaching/src/protocol.js';
import {decodeIdentityBackupPayload,verifyIdentityBackupPayload} from './identity-backup-payload.js';
const utf8=new TextEncoder(),magic=utf8.encode('TMIDENC1'),HEADER=40,ITERATIONS=600000;
export {provider as identityArchiveCryptoProvider,keyFor as deriveIdentityArchiveKey};
export const IDENTITY_ARCHIVE_KDF_ITERATIONS=ITERATIONS;
export const IDENTITY_BACKUP_FILE_MAX_BYTES=504;
const fail=(code,cause)=>{throw Object.assign(Error(code,{cause}),{code});};
function provider(value){if(!value?.subtle||typeof value.getRandomValues!=='function')fail('IDENTITY_BACKUP_CRYPTO_UNAVAILABLE');return value;}
async function keyFor(password,salt,crypto,exporting){
 if(typeof password!=='string'||!password.length||exporting&&Array.from(password).length<12)fail('IDENTITY_BACKUP_PASSWORD_INVALID');
 const raw=utf8.encode(password);
 try{if(raw.length>1024||new TextDecoder().decode(raw)!==password)fail('IDENTITY_BACKUP_PASSWORD_INVALID');const material=await crypto.subtle.importKey('raw',raw,'PBKDF2',false,['deriveKey']);return await crypto.subtle.deriveKey({name:'PBKDF2',hash:'SHA-256',iterations:ITERATIONS,salt},material,{name:'AES-GCM',length:256},false,['encrypt','decrypt']);}
 finally{raw.fill(0);}
}
export function isEncryptedIdentityBackup(value){return value instanceof Uint8Array&&value.length>=8&&magic.every((byte,index)=>value[index]===byte);}
export async function encryptIdentityBackup(signed,author,password,{cryptoProvider=globalThis.crypto}={}){
 if(!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64))fail('IDENTITY_BACKUP_CONTENT_INVALID');
 const retained=[signed[0].slice(),signed[1].slice()];let identity,plain;
 try{
  identity=await verifyIdentityBackupPayload(retained,author);plain=encode(retained);if(plain.length>448)fail('IDENTITY_BACKUP_CONTENT_INVALID');
  const crypto=provider(cryptoProvider),header=new Uint8Array(HEADER);header.set(magic);new DataView(header.buffer).setUint32(8,ITERATIONS,false);crypto.getRandomValues(header.subarray(12,28));crypto.getRandomValues(header.subarray(28,40));
  const key=await keyFor(password,header.slice(12,28),crypto,true),cipher=new Uint8Array(await crypto.subtle.encrypt({name:'AES-GCM',iv:header.slice(28,40),additionalData:header,tagLength:128},key,plain));
  const result=new Uint8Array(HEADER+cipher.length);result.set(header);result.set(cipher,HEADER);return result;
 }finally{retained[0].fill(0);retained[1].fill(0);identity?.privateKey.fill(0);plain?.fill(0);}
}
// Decryption authenticates the archived author, not the currently connected
// target device. Restore UI must compare the fingerprint and require its own
// device-side key validation, atomic storage and generation advancement.
export async function decryptIdentityBackup(input,password,{cryptoProvider=globalThis.crypto}={}){
 if(!isEncryptedIdentityBackup(input)||input.length<=HEADER+16||input.length>IDENTITY_BACKUP_FILE_MAX_BYTES)fail('IDENTITY_BACKUP_FORMAT_INVALID');
 const raw=input.slice(),header=raw.slice(0,HEADER);if(new DataView(header.buffer).getUint32(8,false)!==ITERATIONS)fail('IDENTITY_BACKUP_PARAMETERS_UNSUPPORTED');
 const crypto=provider(cryptoProvider),key=await keyFor(password,header.slice(12,28),crypto,false);let plain,payload;
 try{
  try{plain=new Uint8Array(await crypto.subtle.decrypt({name:'AES-GCM',iv:header.slice(28,40),additionalData:header,tagLength:128},key,raw.subarray(HEADER)));}catch(error){fail('IDENTITY_BACKUP_AUTH_FAILED',error);}
  const signed=decode(plain,448);if(!Array.isArray(signed)||signed.length!==2)fail('IDENTITY_BACKUP_CONTENT_INVALID');
  payload=decodeIdentityBackupPayload(signed[0]);
  const identity=await verifyIdentityBackupPayload(signed,{verified:true,publicKey:payload.publicKey,generation:payload.generation});
  return {identity,signed:[signed[0].slice(),signed[1].slice()]};
 }finally{plain?.fill(0);payload?.privateKey.fill(0);}
}
export function clearDecryptedIdentityBackup(value){value?.identity?.privateKey?.fill(0);value?.signed?.[0]?.fill(0);value?.signed?.[1]?.fill(0);}
