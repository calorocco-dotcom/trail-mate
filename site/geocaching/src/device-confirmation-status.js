import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {readCurrentDeviceAuthor} from './current-device-author.js';
import {bytes,equal,integer,decode} from './protocol.js';

async function confirmationAuthor(session,author,signal){
 return readCurrentDeviceAuthor(session,author,{signal});
}

function confirmationState(value,binding){
 if(!Array.isArray(value)||value.length!==8||!equal(value[0],binding.id)||!equal(value[1],binding.sha256)||
    value[2]!==binding.authorGeneration||!integer(value[3],0,2))throw Error('Invalid device confirmation status');
 if(value[3]===1){
  if(!equal(value[4],binding.cacheId)||value[5]!==binding.revision||!equal(value[6],binding.revisionHash)||value[7]!==1)
   throw Error('Device confirmation does not match durable publication');
 }else if(value[4]!==null||value[5]!==null||value[6]!==null||value[7]!==0)throw Error('Invalid pending device confirmation');
 return {binding,phase:value[3],confirmed:value[3]===1};
}

export async function beginDevicePublicationConfirmation(session,transfer,author,{signal,timeoutMs=90000,retryMs=500,
 sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms)),now=()=>performance.now()}={}){
 const proof=transfer?.receipt;
 if(!transfer?.sealed||!bytes(transfer.id,16)||!transfer.id.some(Boolean)||!bytes(transfer.sha256,32)||
    !integer(transfer.authorGeneration,1,0xffffffff)||transfer.authorGeneration!==author?.generation||
    !(proof?.raw instanceof Uint8Array)||proof.raw.length>192||!bytes(proof.signature,64)||!bytes(proof.publicKey,64)||!bytes(proof.epoch,16))
  throw Error('Invalid confirmation upload binding');
 if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('Invalid confirmation admission budget');
 const raw=proof.raw.slice(),signature=proof.signature.slice(),publicKey=proof.publicKey.slice(),epoch=proof.epoch.slice(),fields=decode(raw,192);
 if(!Array.isArray(fields)||fields.length!==8||fields[0]!==1||!equal(fields[2],epoch)||!bytes(fields[3],32)||
    !integer(fields[4],1,0xffffffff)||!bytes(fields[5],32))throw Error('Invalid confirmation receipt binding');
 const binding={id:transfer.id.slice(),sha256:transfer.sha256.slice(),authorGeneration:transfer.authorGeneration,
  cacheId:fields[3].slice(),revision:fields[4],revisionHash:fields[5].slice()};
 const expected={verified:author.verified,publicKey:author.publicKey?.slice(),generation:author.generation};
 await confirmationAuthor(session,expected,signal);
 const deadline=now()+timeoutMs;
 try{
  for(;;){
   if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');
   let response;
   try{response=await session.request(OPERATION.CACHE_CONFIRM_BEGIN,[binding.id.slice(),binding.sha256.slice(),binding.authorGeneration,raw.slice(),signature.slice(),publicKey.slice(),epoch.slice()]);}
   catch(error){
    // Only an explicit Busy reply proves this attempt was not admitted.
    if(error.code!==5||now()>=deadline)throw error;
    await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;
   }
   return confirmationState(response,binding);
  }
 }catch(error){throw Object.assign(Error(error.message),{code:'CONFIRMATION_UNCONFIRMED',binding,cause:error});}
}

export async function readDevicePublicationConfirmation(session,binding,author,{signal}={}){
 if(!bytes(binding?.id,16)||!bytes(binding?.sha256,32)||!bytes(binding?.cacheId,32)||!bytes(binding?.revisionHash,32)||
    !integer(binding?.revision,1,0xffffffff)||binding.authorGeneration!==author?.generation)throw Error('Invalid confirmation recovery binding');
 const frozen={...binding,id:binding.id.slice(),sha256:binding.sha256.slice(),cacheId:binding.cacheId.slice(),revisionHash:binding.revisionHash.slice()};
 await confirmationAuthor(session,{...author,publicKey:author.publicKey?.slice()},signal);
 let value;
 try{value=await session.request(OPERATION.CACHE_CONFIRM_STATUS,[frozen.id.slice()]);}
 catch(error){if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;return {confirmed:false,busy:true,binding:frozen};}
 const result=confirmationState(value,frozen);
 if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');
 return result;
}
