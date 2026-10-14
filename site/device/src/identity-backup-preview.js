import {decryptIdentityBackup,clearDecryptedIdentityBackup} from './identity-backup-file.js';
import {proveDeviceAuthor} from './author-identity.js';
import {equal} from '../../geocaching/src/protocol.js';
import {OPERATION} from './management-protocol.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const fail=code=>{throw Object.assign(Error(code),{code});};

// The preview exposes public metadata only. Its private plaintext stays in this
// short-lived closure, never in component state, storage or a downloadable URL.
export async function previewEncryptedDeviceIdentity(input,password,{session,operations=[],signal,cryptoProvider=globalThis.crypto}={}){
 let decoded,closed=false,target,submitting=false;
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');if(closed)fail('IDENTITY_BACKUP_PREVIEW_CLOSED');};
 const close=()=>{closed=true;clearDecryptedIdentityBackup(decoded);decoded=undefined;signal?.removeEventListener('abort',close);};
 try{
  check();decoded=await decryptIdentityBackup(input,password,{cryptoProvider});check();
  if(session){target=await proveDeviceAuthor(session,{signal});check();}
  const identity=decoded.identity;
  const supportsFormat=async()=>{
   if(identity.format===0)return true;
   if(!session||!operations.includes(OPERATION.IDENTITY_RESTORE_FORMATS))return false;
   const reply=await session.request(OPERATION.IDENTITY_RESTORE_FORMATS,[],{signal});check();
   if(!Array.isArray(reply)||!reply.length||reply.length>2||new Set(reply).size!==reply.length||reply.some(value=>value!==0&&value!==1))fail('IDENTITY_RESTORE_FORMAT_UNSUPPORTED');
   return reply.includes(identity.format);
  };
  const restoreSupported=await supportsFormat();check();
  const metadata=Object.freeze({identityHash:hex(identity.identityHash),delivery:hex(identity.delivery),publicKey:hex(identity.publicKey),format:identity.format,restoreSupported,backupGeneration:identity.generation,
   targetPublicKey:target?hex(target.publicKey):null,targetIdentityHash:target?hex(target.identityHash):null,targetGeneration:target?.generation??null,
   changesIdentity:target?!equal(target.publicKey,identity.publicKey):null});
  signal?.addEventListener('abort',close,{once:true});check();
  return Object.freeze({metadata,close,get closed(){return closed;},
   async revalidateTarget(){
    if(submitting)fail('IDENTITY_RESTORE_BUSY');
    check();if(!session||!target)fail('IDENTITY_BACKUP_TARGET_REQUIRED');
    const current=await proveDeviceAuthor(session,{signal});check();
    if(!equal(current.publicKey,target.publicKey)||current.generation!==target.generation)fail('IDENTITY_BACKUP_TARGET_CHANGED');
    return metadata;
   },
   async restore({consent=false}={}){
    check();if(consent!==true)fail('IDENTITY_BACKUP_CONSENT_REQUIRED');
    if(!session||!target)fail('IDENTITY_BACKUP_TARGET_REQUIRED');
    if(!metadata.restoreSupported)fail('IDENTITY_RESTORE_FORMAT_UNSUPPORTED');
    if(target.generation===0xffffffff)fail('IDENTITY_RESTORE_GENERATION_EXHAUSTED');
    if(submitting)fail('IDENTITY_RESTORE_BUSY');submitting=true;
    let sent=false,payload,signature;
    const expectation=Object.freeze({publicKey:metadata.publicKey,identityHash:metadata.identityHash,delivery:metadata.delivery,generation:target.generation+1});
    try{
     const current=await proveDeviceAuthor(session,{signal});check();
     if(!equal(current.publicKey,target.publicKey)||current.generation!==target.generation)fail('IDENTITY_BACKUP_TARGET_CHANGED');
     if(!await supportsFormat())fail('IDENTITY_RESTORE_FORMAT_UNSUPPORTED');
     payload=decoded.signed[0].slice();signature=decoded.signed[1].slice();
     sent=true;
     const response=await session.request(OPERATION.IDENTITY_BACKUP_RESTORE,[target.generation,target.publicKey,payload,signature,true],{signal});check();
     if(!Array.isArray(response)||response.length!==2||![0,1].includes(response[0])||response[1]!==expectation.generation)fail('IDENTITY_RESTORE_RESPONSE_INVALID');
     return Object.freeze({state:response[0]===0?'reboot-required':'indeterminate',expectation});
    }catch(error){
     // Explicit device rejection is distinct from a lost response. Internal
     // errors, malformed replies and cancellation after sending remain unknown.
     if(sent&&!(Number.isInteger(error.code)&&error.code>=1&&error.code<=10))throw Object.assign(Error('IDENTITY_RESTORE_OUTCOME_UNKNOWN',{cause:error}),{code:'IDENTITY_RESTORE_OUTCOME_UNKNOWN',expectation});
     throw error;
    }finally{payload?.fill(0);signature?.fill(0);submitting=false;close();}
   }});
 }catch(error){close();throw error;}
}
