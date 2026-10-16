import {TmsDocument,TMS_LIMIT} from './tms-document.js';
const utf8=new TextEncoder(),magic=utf8.encode('TMBKENC1');
const HEADER=40,TAG=16,ITERATIONS=600000;
const sourceMagic=utf8.encode('TMBKMETA1'),SOURCE_MAX=1024,SOURCE_HEADER=11;
export const ENCRYPTED_BACKUP_MAX_BYTES=HEADER+TAG+SOURCE_HEADER+SOURCE_MAX+TMS_LIMIT.documentBytes;
function sourceInfo(value){
 if(!value||typeof value!=='object'||Array.isArray(value))throw Error('备份来源信息无效');
 const text=(value,max)=>{if(value===null||value===undefined)return null;if(typeof value!=='string'||utf8.encode(value).length>max||/[\u0000-\u001f\u007f]/.test(value))throw Error('备份来源文字无效');return value;};
 const createdAt=text(value.createdAt,32),model=text(value.model,64),firmware=text(value.firmware,64);
 if(!createdAt||!Number.isFinite(Date.parse(createdAt))||new Date(createdAt).toISOString()!==createdAt)throw Error('备份导出时间无效');
 const authorFingerprint=value.authorFingerprint??null,authorGeneration=value.authorGeneration??null;
 if((authorFingerprint===null)!==(authorGeneration===null)||authorFingerprint!==null&&
  (typeof authorFingerprint!=='string'||!/^[0-9a-f]{64}$/.test(authorFingerprint)||!Number.isInteger(authorGeneration)||authorGeneration<1||authorGeneration>0xffffffff))throw Error('备份作者信息无效');
 return Object.freeze({version:1,createdAt,model,firmware,authorFingerprint,authorGeneration});
}
export function isEncryptedConfigBackup(bytes){return bytes instanceof Uint8Array&&bytes.length>=8&&magic.every((byte,i)=>bytes[i]===byte);}
function backupError(code,message,cause){const error=Error(message,{cause});error.code=code;return error;}
function provider(cryptoProvider){if(!cryptoProvider?.subtle||!cryptoProvider.getRandomValues)throw backupError('BACKUP_CRYPTO_UNAVAILABLE','此浏览器无法使用本地备份加密');return cryptoProvider;}
function passwordBytes(password,exporting){
 if(typeof password!=='string'||!password.length)throw backupError('BACKUP_PASSWORD_INVALID','请输入备份口令');
 const bytes=utf8.encode(password);
 if(new TextDecoder().decode(bytes)!==password||bytes.length>1024)throw backupError('BACKUP_PASSWORD_INVALID','备份口令无效或超过 1024 字节');
 if(exporting&&Array.from(password).length<12)throw backupError('BACKUP_PASSWORD_INVALID','备份口令至少需要 12 个字符');
 return bytes;
}
async function keyFor(password,salt,cryptoProvider,exporting){
 const bytes=passwordBytes(password,exporting);
 try{
  const source=await cryptoProvider.subtle.importKey('raw',bytes,'PBKDF2',false,['deriveKey']);
  return await cryptoProvider.subtle.deriveKey({name:'PBKDF2',salt,iterations:ITERATIONS,hash:'SHA-256'},source,
   {name:'AES-GCM',length:256},false,['encrypt','decrypt']);
 }finally{bytes.fill(0);}
}
export async function encryptConfigBackup(document,password,{cryptoProvider=globalThis.crypto,source}={}){
 if(!(document instanceof TmsDocument))throw Error('配置备份文档无效');
 const info=source===undefined?null:utf8.encode(JSON.stringify(sourceInfo(source)));
 if(info&&info.length>SOURCE_MAX)throw Error('备份来源超过容量限制');
 const crypto=provider(cryptoProvider),raw=document.serialize({kind:'backup'}),header=new Uint8Array(HEADER);
 let plain=raw;
 try{
  if(info){plain=new Uint8Array(SOURCE_HEADER+info.length+raw.length);plain.set(sourceMagic);new DataView(plain.buffer).setUint16(9,info.length,false);plain.set(info,SOURCE_HEADER);plain.set(raw,SOURCE_HEADER+info.length);}
  header.set(magic);new DataView(header.buffer).setUint32(8,ITERATIONS,false);
  crypto.getRandomValues(header.subarray(12,28));crypto.getRandomValues(header.subarray(28,40));
  const key=await keyFor(password,header.slice(12,28),crypto,true);
  const cipher=new Uint8Array(await crypto.subtle.encrypt({name:'AES-GCM',iv:header.slice(28,40),additionalData:header,tagLength:128},key,plain));
  const result=new Uint8Array(HEADER+cipher.length);result.set(header);result.set(cipher,HEADER);return result;
 }finally{raw.fill(0);if(plain!==raw)plain.fill(0);}
}
export async function decryptConfigBackupDetails(input,password,{cryptoProvider=globalThis.crypto}={}){
 if(!(input instanceof Uint8Array)||input.length<=HEADER+TAG||input.length>ENCRYPTED_BACKUP_MAX_BYTES||!isEncryptedConfigBackup(input))throw backupError('BACKUP_FORMAT_INVALID','加密备份格式或容量无效');
 // Version 1 has a fixed cost. Reject hostile KDF costs before deriving a key.
 const bytes=input.slice(),header=bytes.slice(0,HEADER);
 if(new DataView(header.buffer).getUint32(8,false)!==ITERATIONS)throw backupError('BACKUP_PARAMETERS_UNSUPPORTED','不支持的备份加密参数');
 const crypto=provider(cryptoProvider),key=await keyFor(password,header.slice(12,28),crypto,false);
 let plain;
 try{plain=new Uint8Array(await crypto.subtle.decrypt({name:'AES-GCM',iv:header.slice(28,40),additionalData:header,tagLength:128},key,bytes.subarray(HEADER)));}
 catch(error){throw backupError('BACKUP_AUTH_FAILED','备份口令不正确或文件已损坏',error);}
 try{
  let raw=plain,source=null;
  if(sourceMagic.every((byte,i)=>plain[i]===byte)){
   if(plain.length<SOURCE_HEADER)throw Error('来源信息截断');
   const size=new DataView(plain.buffer,plain.byteOffset,plain.byteLength).getUint16(9,false);
   if(!size||size>SOURCE_MAX||SOURCE_HEADER+size>=plain.length)throw Error('来源信息容量无效');
   const value=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(plain.subarray(SOURCE_HEADER,SOURCE_HEADER+size)));
   if(value.version!==1)throw Error('来源版本无效');source=sourceInfo(value);raw=plain.subarray(SOURCE_HEADER+size);
  }
  const document=new TmsDocument(raw);if(document.kind!=='backup')throw Error('文档不是备份');return Object.freeze({document,source});
 }
 catch(error){throw backupError('BACKUP_DOCUMENT_INVALID','加密备份中的配置文档无效',error);}
 finally{plain.fill(0);}
}
export async function decryptConfigBackup(input,password,options={}){return (await decryptConfigBackupDetails(input,password,options)).document;}
