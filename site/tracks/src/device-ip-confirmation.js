import {OPERATION} from '../../device/src/management-protocol.js';
import {validateAuthorInfo} from '../../device/src/author-identity.js';
import {uploadObject} from '../../device/src/object-transfer.js';
import {bytes,equal,integer,decode} from '../../geocaching/src/protocol.js';
import {encodeTrackIPCheckpoint,decodeTrackIPCheckpoint} from './ip-publication-journal.js';
import {verifyTrackManifest} from './manifest.js';
import {verifyTrackReceipt} from './receipt.js';
const active=new WeakSet();

export async function confirmDeviceTrackPublication(session,record,author,{id,manifestId,importHistory=false,operations=[],signal,onProgress=()=>{},
 timeoutMs=90000,retryMs=500,sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms)),now=()=>performance.now()}={}){
 const operation=importHistory?OPERATION.TRACK_IMPORT_HISTORY:OPERATION.TRACK_CONFIRM_PUBLICATION;
 if(![32,33,35,operation].every(op=>operations.includes(op)))throw Error('Device track confirmation capability unavailable');
 if(importHistory&&(!bytes(manifestId,16)||!manifestId.some(Boolean)||equal(id,manifestId)))throw Error('Fixed history manifest upload ID required');
 const historyTransferId=importHistory?manifestId.slice():null;
 if(!session||typeof session.request!=='function'||active.has(session)||!bytes(id,16)||!id.some(Boolean)||!author?.verified||
    !bytes(author.publicKey,64)||!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0||retryMs>=30000||
    typeof sleep!=='function'||typeof now!=='function'||typeof onProgress!=='function')throw Error('Invalid device track confirmation input');
 const retained=decodeTrackIPCheckpoint(encodeTrackIPCheckpoint(record)),account={...author,publicKey:author.publicKey.slice()},transferId=id.slice();
 if(retained.stage!=='confirmed'||!equal(retained.authorKey,account.publicKey)||(!importHistory&&retained.plan.generation!==account.generation)||retained.receipt.length>1100)
  throw Error('Device confirmation requires the matching confirmed track publication');
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};
 active.add(session);
 try{
  check();const signed=[retained.plan.manifest.slice(),retained.signature.slice()];
  const version=await verifyTrackManifest(signed,{expectedAuthor:account.publicKey}),receipt=decode(retained.receipt,1100);
  await verifyTrackReceipt(receipt,{directoryPublicKey:retained.directoryKey,directoryEpoch:retained.epoch,requestId:retained.requestId,manifestSigned:signed});
  const fields=decode(receipt[0],1024);if(!equal(fields[9],retained.delivery))throw Error('Track confirmation directory differs');
  const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',retained.receipt));check();
  const manifestHash=importHistory?new Uint8Array(await crypto.subtle.digest('SHA-256',retained.plan.manifest)):null;
  const current=await validateAuthorInfo(await session.request(OPERATION.AUTHOR_INFO,[]));check();
  if(!equal(current.publicKey,account.publicKey)||current.generation!==account.generation)throw Object.assign(Error('Device identity changed'),{code:'STALE_IDENTITY'});
  const binding={id:transferId,sha256:hash,operationId:retained.plan.operationId,authorGeneration:account.generation,
   objectId:version.objectId,revisionHash:version.revisionHash,revision:version.record[4],
   ...(importHistory?{manifestId:historyTransferId,manifestHash}:{} )};
  const body=()=>[binding.operationId.slice(),binding.id.slice(),binding.sha256.slice(),binding.authorGeneration,retained.directoryKey.slice(),
   retained.delivery.slice(),retained.epoch.slice(),retained.requestId.slice(),binding.objectId.slice(),binding.revisionHash.slice(),binding.revision,
   ...(importHistory?[binding.manifestId.slice(),binding.manifestHash.slice(),retained.signature.slice()]:[])];
  try{
   if(importHistory)await uploadObject(session,retained.plan.manifest,{id:binding.manifestId,kind:4,sha256:binding.manifestHash,signal,onProgress:p=>onProgress({phase:'uploading-manifest',...p})});
   await uploadObject(session,retained.receipt,{id:binding.id,kind:7,sha256:binding.sha256,signal,onProgress:p=>onProgress({phase:'uploading-receipt',...p})});
   const deadline=now()+timeoutMs;
   for(;;){
    check();if(now()>=deadline)throw Error('Track device confirmation timed out');
    let value;
    try{value=await session.request(operation,body());}
    catch(error){if(error.code!==5)throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(value)||value.length!==8||!equal(value[0],binding.operationId)||!equal(value[1],binding.id)||
       value[2]!==binding.authorGeneration||!integer(value[3],0,1))throw Error('Invalid device track confirmation reply');
    if(value[3]===1){
     if(!equal(value[4],binding.objectId)||!equal(value[5],binding.revisionHash)||value[6]!==binding.revision||value[7]!==1)
      throw Error('Device track confirmation is not durable or has different bindings');
     onProgress({phase:'confirmed'});return {...binding,confirmed:true,durable:true};
    }
    if(value[4]!==null||value[5]!==null||value[6]!==0||value[7]!==0)throw Error('Invalid pending device track confirmation');
    onProgress({phase:'confirming'});await sleep(Math.min(retryMs,Math.max(1,deadline-now())));
   }
  }catch(error){throw Object.assign(Error(error.message),{code:'TRACK_CONFIRMATION_UNCONFIRMED',binding,cause:error});}
 }finally{active.delete(session);}
}
