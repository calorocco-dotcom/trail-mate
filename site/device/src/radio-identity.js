import {Identity} from '@reticulum/core';
import {bytes,equal,integer,encode,decode} from '../../geocaching/src/protocol.js';
import {proveDeviceAuthor} from './author-identity.js';
import {OPERATION} from './management-protocol.js';
import {identityArchiveCryptoProvider,deriveIdentityArchiveKey,IDENTITY_ARCHIVE_KDF_ITERATIONS} from './identity-backup-file.js';
const utf8=new TextEncoder(),domain=utf8.encode('TrailMate.Radio.Identity.Backup.v1\0'),type='trailmate.radio.identity.backup.v1',magic=utf8.encode('TMRFENC1'),HEADER=40;
export const RADIO_IDENTITY_KIND=Object.freeze({MESHCORE:1,MESHTASTIC:2});
export const RADIO_IDENTITY_FILE_MAX_BYTES=504;
const copy=value=>new Uint8Array(value),hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const fail=(code,cause,extra={})=>{throw Object.assign(Error(code,{cause}),{code,...extra});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
const nonzero=value=>value.some(Boolean);
function validFields(value){
 const {kind,publicKey,privateKey,sourceAuthor,authorGeneration,radioGeneration,exportSession,exportRequest}=value;
 if(![1,2].includes(kind)||!bytes(publicKey,32)||!nonzero(publicKey)||!bytes(privateKey,kind===1?64:32)||!nonzero(privateKey.subarray(0,32))||
    kind===1&&!nonzero(privateKey.subarray(32))||kind===2&&((privateKey[0]&7)!==0||(privateKey[31]&0xc0)!==0x40)||
    !bytes(sourceAuthor,64)||!nonzero(sourceAuthor.subarray(0,32))||!nonzero(sourceAuthor.subarray(32))||!integer(authorGeneration,1,0xffffffff)||!integer(radioGeneration,0,0xffffffff)||!bytes(exportSession,16)||!bytes(exportRequest,16))fail('RADIO_IDENTITY_PAYLOAD_INVALID');
 return value;
}
export function encodeRadioIdentityBackupPayload(value){validFields(value);return encode([1,type,value.kind,value.publicKey,value.privateKey,value.sourceAuthor,value.authorGeneration,value.radioGeneration,value.exportSession,value.exportRequest]);}
export function decodeRadioIdentityBackupPayload(input){
 if(!(input instanceof Uint8Array)||input.length>320)fail('RADIO_IDENTITY_PAYLOAD_INVALID');
 const raw=copy(input);let row,accepted=false;
 try{row=decode(raw,320);if(!Array.isArray(row)||row.length!==10||row[0]!==1||row[1]!==type)fail('RADIO_IDENTITY_PAYLOAD_INVALID');
  const result=validFields({kind:row[2],publicKey:row[3],privateKey:row[4],sourceAuthor:row[5],authorGeneration:row[6],radioGeneration:row[7],exportSession:row[8],exportRequest:row[9]});accepted=true;return result;
 }finally{raw.fill(0);if(!accepted)row?.[4]?.fill?.(0);}
}
export function radioIdentityBackupSigningBytes(payload){const output=new Uint8Array(domain.length+payload.length);output.set(domain);output.set(payload,domain.length);return output;}
export async function verifyRadioIdentityBackupPayload(signed,expected){
 if(!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64))fail('RADIO_IDENTITY_PAYLOAD_INVALID');
 if(expected&&(!expected.verified||!bytes(expected.publicKey,64)||!integer(expected.generation,1,0xffffffff)))fail('RADIO_IDENTITY_AUTHOR_INVALID');
 const payload=copy(signed[0]),signature=copy(signed[1]),author=expected?copy(expected.publicKey):null,generation=expected?.generation,
  exportSession=expected?.exportSession?copy(expected.exportSession):null,exportRequest=expected?.exportRequest?copy(expected.exportRequest):null;let identity,message,accepted=false;
 try{identity=decodeRadioIdentityBackupPayload(payload);if(author&&(!equal(author,identity.sourceAuthor)||generation!==identity.authorGeneration))fail('RADIO_IDENTITY_AUTHOR_CHANGED');
  if(exportSession&&!equal(exportSession,identity.exportSession)||exportRequest&&!equal(exportRequest,identity.exportRequest))fail('RADIO_IDENTITY_EXPORT_CONTEXT_CHANGED');
  const source=await Identity.fromPublicKey(identity.sourceAuthor);message=radioIdentityBackupSigningBytes(payload);
  if(!await source.validate(signature,message))fail('RADIO_IDENTITY_SIGNATURE_INVALID');accepted=true;return identity;
 }finally{payload.fill(0);signature.fill(0);message?.fill(0);if(!accepted)identity?.privateKey.fill(0);}
}
export async function encryptRadioIdentityBackup(signed,author,password,{cryptoProvider=globalThis.crypto}={}){
 const retained=Array.isArray(signed)?signed.map(value=>value instanceof Uint8Array?copy(value):value):signed;let identity,plain;
 try{identity=await verifyRadioIdentityBackupPayload(retained,author);plain=encode(retained);if(plain.length>448)fail('RADIO_IDENTITY_PAYLOAD_INVALID');
  const crypto=identityArchiveCryptoProvider(cryptoProvider),header=new Uint8Array(HEADER);header.set(magic);new DataView(header.buffer).setUint32(8,IDENTITY_ARCHIVE_KDF_ITERATIONS,false);
  crypto.getRandomValues(header.subarray(12,28));crypto.getRandomValues(header.subarray(28,40));
  const key=await deriveIdentityArchiveKey(password,header.slice(12,28),crypto,true),cipher=new Uint8Array(await crypto.subtle.encrypt({name:'AES-GCM',iv:header.slice(28,40),additionalData:header,tagLength:128},key,plain));
  const output=new Uint8Array(HEADER+cipher.length);output.set(header);output.set(cipher,HEADER);return output;
 }finally{retained?.forEach?.(value=>value?.fill?.(0));identity?.privateKey.fill(0);plain?.fill(0);}
}
export async function decryptRadioIdentityBackup(input,password,{cryptoProvider=globalThis.crypto}={}){
 if(!(input instanceof Uint8Array)||input.length<=HEADER+16||input.length>RADIO_IDENTITY_FILE_MAX_BYTES||!magic.every((byte,index)=>input[index]===byte))fail('RADIO_IDENTITY_FILE_INVALID');
 const raw=copy(input),header=raw.slice(0,HEADER);if(new DataView(header.buffer).getUint32(8,false)!==IDENTITY_ARCHIVE_KDF_ITERATIONS)fail('IDENTITY_BACKUP_PARAMETERS_UNSUPPORTED');
 const crypto=identityArchiveCryptoProvider(cryptoProvider),key=await deriveIdentityArchiveKey(password,header.slice(12,28),crypto,false);let plain,signed;
 try{try{plain=new Uint8Array(await crypto.subtle.decrypt({name:'AES-GCM',iv:header.slice(28,40),additionalData:header,tagLength:128},key,raw.subarray(HEADER)));}catch(error){fail('IDENTITY_BACKUP_AUTH_FAILED',error);}
  signed=decode(plain,448);const identity=await verifyRadioIdentityBackupPayload(signed);return {identity,signed:[copy(signed[0]),copy(signed[1])]};
 }finally{plain?.fill(0);signed?.forEach?.(value=>value?.fill?.(0));}
}
export function clearRadioIdentityBackup(value){value?.identity?.privateKey?.fill(0);value?.signed?.forEach?.(bytes=>bytes?.fill?.(0));}
function requireOperations(operations,required){if(!Array.isArray(operations)||!required.every(operation=>operations.includes(operation)))fail('RADIO_IDENTITY_UNSUPPORTED');}
export async function readDeviceRadioIdentity(session,kind,author,{operations,signal}={}){
 requireOperations(operations,[OPERATION.RADIO_IDENTITY_INFO]);if(![1,2].includes(kind)||!author?.verified||!integer(author.generation,1,0xffffffff))fail('RADIO_IDENTITY_AUTHOR_INVALID');
 check(signal);const row=await session.request(OPERATION.RADIO_IDENTITY_INFO,[author.generation,kind],{signal});check(signal);
 if(!Array.isArray(row)||row.length!==4||row[0]!==kind||![0,1].includes(row[1])||!integer(row[2],0,0xffffffff)||(row[1]===0?row[3]!==null:!bytes(row[3],32)||!nonzero(row[3])))fail('RADIO_IDENTITY_RESPONSE_INVALID');
 return Object.freeze({kind,present:row[1]===1,generation:row[2],publicKey:row[3]?copy(row[3]):null});
}
export async function exportEncryptedRadioIdentity(session,kind,password,{operations,consent=false,signal,cryptoProvider=globalThis.crypto}={}){
 requireOperations(operations,[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.RADIO_IDENTITY_BACKUP_EXPORT]);if(consent!==true)fail('IDENTITY_BACKUP_CONSENT_REQUIRED');if(![1,2].includes(kind))fail('RADIO_IDENTITY_PAYLOAD_INVALID');
 // Reject invalid passwords/providers before any private-material USB response.
 const crypto=identityArchiveCryptoProvider(cryptoProvider),probe=utf8.encode(password??'');
 try{if(typeof password!=='string'||Array.from(password).length<12||probe.length>1024||new TextDecoder().decode(probe)!==password)fail('IDENTITY_BACKUP_PASSWORD_INVALID');}finally{probe.fill(0);}
 const exportSession=copy(session.id),exportRequest=crypto.getRandomValues(new Uint8Array(16));if(!bytes(exportSession,16)||!bytes(exportRequest,16))fail('RADIO_IDENTITY_AUTHOR_INVALID');
 check(signal);const proven=await proveDeviceAuthor(session,{signal}),author={...proven,exportSession,exportRequest};let signed,identity;
 try{check(signal);signed=await session.request(OPERATION.RADIO_IDENTITY_BACKUP_EXPORT,[author.generation,kind,true],{signal,requestId:exportRequest});check(signal);
  identity=await verifyRadioIdentityBackupPayload(signed,author);if(identity.kind!==kind)fail('RADIO_IDENTITY_RESPONSE_INVALID');
  const output=await encryptRadioIdentityBackup(signed,author,password,{cryptoProvider:crypto});check(signal);
  return {bytes:output,kind,publicKey:hex(identity.publicKey),author};
 }finally{identity?.privateKey.fill(0);signed?.forEach?.(value=>value?.fill?.(0));}
}
export async function restoreRadioIdentityBackup(session,decoded,target,current,{operations,consent=false,signal}={}){
 requireOperations(operations,[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE,OPERATION.RADIO_IDENTITY_INFO,OPERATION.RADIO_IDENTITY_BACKUP_RESTORE]);
 if(consent!==true)fail('IDENTITY_BACKUP_CONSENT_REQUIRED');if(!target?.verified||!bytes(target.publicKey,64)||!integer(target.generation,1,0xffffffff)||!current||current.kind!==decoded?.identity?.kind||
    !integer(current.generation,0,0xfffffffe)||typeof current.present!=='boolean'||(current.present?!bytes(current.publicKey,32):current.publicKey!==null))fail('RADIO_IDENTITY_TARGET_INVALID');
 const actor=copy(target.publicKey),authorGeneration=target.generation,kind=current.kind,expectedGeneration=current.generation,expectedPublic=current.publicKey?copy(current.publicKey):null;
 let payload,signature,sent=false;const expectation=Object.freeze({kind,publicKey:hex(decoded.identity.publicKey),generation:expectedGeneration+1,authorPublicKey:hex(actor),authorGeneration});
 try{check(signal);payload=copy(decoded.signed[0]);signature=copy(decoded.signed[1]);
  const verified=await verifyRadioIdentityBackupPayload([payload,signature]);
  const matches=verified.kind===kind&&hex(verified.publicKey)===expectation.publicKey;verified.privateKey.fill(0);if(!matches)fail('RADIO_IDENTITY_PAYLOAD_INVALID');check(signal);
  const currentAuthor=await proveDeviceAuthor(session,{signal});check(signal);
  if(!equal(actor,currentAuthor.publicKey)||authorGeneration!==currentAuthor.generation)fail('RADIO_IDENTITY_AUTHOR_CHANGED');
  const currentRadio=await readDeviceRadioIdentity(session,kind,currentAuthor,{operations,signal});
  if(currentRadio.generation!==expectedGeneration||currentRadio.present!==Boolean(expectedPublic)||(expectedPublic&&!equal(currentRadio.publicKey,expectedPublic)))fail('RADIO_IDENTITY_TARGET_CHANGED');
  check(signal);
  sent=true;const reply=await session.request(OPERATION.RADIO_IDENTITY_BACKUP_RESTORE,[authorGeneration,actor,payload,signature,expectedGeneration,expectedPublic,true],{signal});check(signal);
  if(!Array.isArray(reply)||reply.length!==4||![0,1].includes(reply[0])||reply[1]!==kind||reply[2]!==expectation.generation||!bytes(reply[3],32)||hex(reply[3])!==expectation.publicKey)fail('RADIO_IDENTITY_RESPONSE_INVALID');
  return Object.freeze({state:reply[0]===0?'reboot-required':'indeterminate',expectation});
 }catch(error){if(sent&&!(Number.isInteger(error.code)&&error.code>=1&&error.code<=10))fail('RADIO_IDENTITY_OUTCOME_UNKNOWN',error,{expectation});throw error;}
 finally{payload?.fill(0);signature?.fill(0);clearRadioIdentityBackup(decoded);}
}
export async function verifyRestoredRadioIdentity(session,expectation,{operations,signal}={}){
 const author=await proveDeviceAuthor(session,{signal});check(signal);if(hex(author.publicKey)!==expectation.authorPublicKey||author.generation!==expectation.authorGeneration)fail('RADIO_IDENTITY_AUTHOR_CHANGED');
 const radio=await readDeviceRadioIdentity(session,expectation.kind,author,{operations,signal});
 if(!radio.present||radio.generation!==expectation.generation||hex(radio.publicKey)!==expectation.publicKey)fail('RADIO_IDENTITY_NOT_CONFIRMED');return radio;
}
