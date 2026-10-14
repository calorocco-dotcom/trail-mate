import {proveDeviceAuthor} from './author-identity.js';
import {OPERATION} from './management-protocol.js';
import {encryptIdentityBackup} from './identity-backup-file.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
export async function exportEncryptedDeviceIdentity(session,password,{consent=false,signal,cryptoProvider=globalThis.crypto}={}){
 if(consent!==true)fail('IDENTITY_BACKUP_CONSENT_REQUIRED');
 if(typeof password!=='string'||Array.from(password).length<12||new TextEncoder().encode(password).length>1024)fail('IDENTITY_BACKUP_PASSWORD_INVALID');
 const passwordBytes=new TextEncoder().encode(password);const validPassword=new TextDecoder().decode(passwordBytes)===password;passwordBytes.fill(0);if(!validPassword)fail('IDENTITY_BACKUP_PASSWORD_INVALID');
 if(!cryptoProvider?.subtle||typeof cryptoProvider.getRandomValues!=='function')fail('IDENTITY_BACKUP_CRYPTO_UNAVAILABLE');
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
 check();const author=await proveDeviceAuthor(session,{signal});check();let signed;
 try{
  signed=await session.request(OPERATION.IDENTITY_BACKUP_EXPORT,[author.generation,true],{signal});check();
  const bytes=await encryptIdentityBackup(signed,author,password,{cryptoProvider});check();
  return {bytes,author};
 }finally{if(Array.isArray(signed)){signed[0]?.fill?.(0);signed[1]?.fill?.(0);}}
}
