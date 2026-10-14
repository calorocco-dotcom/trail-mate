import {CAPABILITY} from '../../device/src/hostlink.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {bytes,equal,integer,encode} from './protocol.js';
const invalid=()=>{throw Object.assign(Error('Signature upload recovery binding mismatch'),{code:'GEO_SAVE_OWNER_CONFLICT'});};

// Read-only metadata query. A correlation owner is not an identity credential;
// author proof and all content bindings must still be checked before using it.
export async function readSignatureRecovery(session,author,{capabilities=0,operations=[],signal,retryMs=250,timeoutMs=5000,kind=5}={}){
 if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))invalid();
 author={...author,publicKey:author.publicKey.slice()};operations=operations.slice();
 if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)invalid();
 if(kind!==5&&kind!==6)invalid();
 const capability=kind===5?CAPABILITY.SHARED_SIGNATURE_RECOVERY:CAPABILITY.SHARED_CONFIRMATION_RECOVERY;
 if(!(capabilities&capability)||!operations.includes(OPERATION.TRANSFER_LIST_OWNED))return null;
 const deadline=performance.now()+timeoutMs;let reply;
 for(;;){
  if(signal?.aborted)throw signal.reason;
  try{reply=await session.request(OPERATION.TRANSFER_LIST_OWNED,[kind,null]);if(signal?.aborted)throw signal.reason;break;}
  catch(error){if(error.code!==MANAGEMENT_STATUS.BUSY||performance.now()>=deadline)throw error;}
  await new Promise(resolve=>setTimeout(resolve,retryMs));
 }
 if(!Array.isArray(reply)||reply.length!==3||!integer(reply[0],0,0xffffffff)||reply[1]!==null)invalid();
 const row=reply[2];if(row===null)return null;
 if(!Array.isArray(row)||row.length!==9||!bytes(row[0],16)||!row[0].some(Boolean)||row[1]!==kind||!integer(row[2],1,kind===5?4096:4166)||
    !bytes(row[3],32)||row[4]!==author.generation||row[5]!==1||row[6]!==row[2]||!bytes(row[7],16)||!row[7].some(Boolean)||!bytes(row[8],16))invalid();
 return {id:row[0].slice(),total:row[2],hash:row[3].slice(),authorGeneration:row[4],owner:row[7].slice(),draftId:row[8].slice(),media:reply[0]};
}
export async function recoverSignatureOwner(session,record,author,options={}){
 if(!record||record.stage!=='signing'||record.authorGeneration!==author.generation||!equal(record.authorKey,author.publicKey))return null;
 author={...author,publicKey:author.publicKey.slice()};
 record={...record,transferId:record.transferId.slice(),hash:record.hash.slice(),draftId:record.draftId.slice(),raw:record.raw.slice()};
 const row=await readSignatureRecovery(session,author,options);if(!row)return null;
 if(!equal(row.id,record.transferId)||row.total!==record.raw.length||!equal(row.hash,record.hash)||!equal(row.draftId,record.draftId)||
    !equal(new Uint8Array(await crypto.subtle.digest('SHA-256',record.raw)),row.hash))invalid();
 return row.owner.slice();
}
export async function recoverConfirmationOwner(session,record,author,options={}){
 if(!record?.deviceSync||!['pending','confirmed'].includes(record.deviceSync.stage)||
    (record.deviceSync.generation??record.authorGeneration)!==author.generation||!equal(record.authorKey,author.publicKey))return null;
 author={...author,publicKey:author.publicKey.slice()};
 const id=record.deviceSync.id.slice(),draftId=record.draftId.slice(),source=encode([record.raw.slice(),record.signature.slice()]);
 const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
 const row=await readSignatureRecovery(session,author,{...options,kind:6});if(!row)return null;
 if(!equal(row.id,id)||row.total!==source.length||!equal(row.hash,hash)||!equal(row.draftId,draftId))invalid();
 return row.owner.slice();
}
