import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {readCurrentDeviceAuthor} from './current-device-author.js';
import {uploadObject} from '../../device/src/object-transfer.js';
import {bytes,equal,integer,decode,verifySigned} from './protocol.js';
const required=[OPERATION.CACHE_SIGN_BEGIN,OPERATION.CACHE_SIGN_STATUS,OPERATION.CACHE_SIGN_READ];
const checkAbort=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Signature request paused','AbortError');};
export async function uploadUnsignedDeviceCache(session,source,author,{id,operations=[],signal,onProgress}={}){
 if(![32,33,35,...required].every(op=>operations.includes(op)))throw Error('Device local signing capability unavailable');
 if(!(source instanceof Uint8Array)||!source.length||source.length>4096||!bytes(id,16)||!id.some(b=>b))throw Error('Invalid unsigned cache upload');
 const content=source.slice(),transferId=id.slice(),expectedAuthor={verified:author?.verified,publicKey:author?.publicKey?.slice(),generation:author?.generation};
 const record=decode(content,4096);
 if(!Array.isArray(record)||record.length!==16||record[0]!==1||!bytes(record[1],64)||!equal(record[1],expectedAuthor.publicKey))throw Error('Unsigned cache author mismatch');
 const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',content));
 await currentAuthor(session,expectedAuthor,signal);
 await uploadObject(session,content,{id:transferId,kind:5,sha256:hash,signal,onProgress});
 return {id:transferId,sha256:hash,bytes:content.length,sealed:true};
}
async function currentAuthor(session,author,signal){
 return readCurrentDeviceAuthor(session,author,{signal});
}
function receipt(value,binding){
 if(!Array.isArray(value)||value.length!==5||!equal(value[0],binding.operationId)||!equal(value[1],binding.hash)||value[2]!==binding.generation||
    !integer(value[3],0,2)||!integer(value[4],0,4166)||(value[3]===1?value[4]<1:value[4]!==0))throw Error('Invalid signature task receipt');
 return {...binding,phase:value[3],total:value[4]};
}
export async function beginDeviceGeocacheSignature(session,transfer,author,{operationId,draftId,draftGeneration,operations=[],signal,timeoutMs=90000,retryMs=500,
 sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms)),now=()=>performance.now()}={}){
 if(!required.every(op=>operations.includes(op)))throw Error('Device local signing capability unavailable');
 if(!transfer?.sealed||!bytes(transfer.id,16)||!transfer.id.some(b=>b)||!bytes(transfer.sha256,32)||!integer(transfer.bytes,1,4096)||!bytes(operationId,16)||!operationId.some(b=>b))throw Error('Invalid signature upload binding');
 const binding={operationId:operationId.slice(),hash:transfer.sha256.slice(),generation:author?.generation};
 const expectedAuthor={verified:author?.verified,publicKey:author?.publicKey?.slice(),generation:author?.generation};
 const id=transfer.id.slice(),size=transfer.bytes;
 const draft=draftId?.slice();let draftVersion;
 if(draftId!==undefined||draftGeneration!==undefined){
  if(!bytes(draft,16))throw Error('Invalid signature draft binding');
  try{draftVersion=BigInt(draftGeneration);}catch{throw Error('Invalid signature draft generation');}
  if(draftVersion<1n||draftVersion>0xffffffffffffffffn)throw Error('Invalid signature draft generation');
  // CMP1 requires the shortest integer form. The upstream MsgPack encoder
  // encodes even 1n as uint64; our strict decoder correctly rejects that.
  if(draftVersion<=BigInt(Number.MAX_SAFE_INTEGER))draftVersion=Number(draftVersion);
 }
 if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('Invalid signature admission budget');
 await currentAuthor(session,expectedAuthor,signal);checkAbort(signal);
 const deadline=now()+timeoutMs;
 try{
  for(;;){
   checkAbort(signal);
   try{const request=[id.slice(),binding.hash.slice(),size,binding.generation,binding.operationId.slice()];if(draft)request.push(draft.slice(),draftVersion);return receipt(await session.request(OPERATION.CACHE_SIGN_BEGIN,request),binding);}
   catch(error){
    // A received Busy means storage initialization has not admitted this ID.
    // Never retry a lost reply or any transport error.
    if(error.code!==5||now()>=deadline)throw error;
    await sleep(Math.min(retryMs,Math.max(1,deadline-now())));
   }
  }
 }
 catch(error){throw Object.assign(Error(error.message),{code:'SIGNATURE_UNCONFIRMED',binding,cause:error});}
}
export async function readDeviceGeocacheSignature(session,binding,source,author,{signal}={}){
 if(!(source instanceof Uint8Array)||!source.length||source.length>4096||!bytes(binding?.operationId,16)||!bytes(binding?.hash,32)||!integer(binding?.generation,1,0xffffffff))throw Error('Invalid signature recovery binding');
 const original=source.slice(),expected={operationId:binding.operationId.slice(),hash:binding.hash.slice(),generation:binding.generation};
 const expectedAuthor={verified:author?.verified,publicKey:author?.publicKey?.slice(),generation:author?.generation};
 if(!equal(new Uint8Array(await crypto.subtle.digest('SHA-256',original)),expected.hash))throw Error('Signature source hash mismatch');
 const current=await currentAuthor(session,expectedAuthor,signal);if(current.generation!==expected.generation)throw Error('Signature identity generation changed');
 let response;
 try{response=await session.request(OPERATION.CACHE_SIGN_STATUS,[expected.operationId.slice()]);}
 catch(error){checkAbort(signal);if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;return {ready:false,busy:true,binding:expected};}
 const state=receipt(response,expected);checkAbort(signal);
 if(state.phase!==1)return {ready:false,phase:state.phase,binding:state};
 const output=new Uint8Array(state.total);let offset=0;
 while(offset<output.length){
  checkAbort(signal);let value;
  try{value=await session.request(OPERATION.CACHE_SIGN_READ,[expected.operationId.slice(),offset,320]);}
  catch(error){checkAbort(signal);if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;return {ready:false,busy:true,binding:expected};}
  checkAbort(signal);
  if(!Array.isArray(value)||value.length!==5||!equal(value[0],expected.operationId)||value[1]!==offset||value[2]!==output.length||!equal(value[3],expected.hash)||
     !(value[4] instanceof Uint8Array)||!value[4].length||value[4].length>320||value[4].length>output.length-offset)throw Error('Invalid signature result chunk');
  output.set(value[4],offset);offset+=value[4].length;
 }
 const signed=decode(output,4166),verified=await verifySigned(signed);
 if(!equal(signed[0],original)||!equal(verified.record[1],current.publicKey))throw Error('Signed result differs from requested device content');
 return {ready:true,signed,verified,binding:state};
}
