import {Identity} from '@reticulum/core';
import {bytes,integer,equal,encode,decode,validText} from '../../geocaching/src/protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {verifyTrackManifest} from './manifest.js';
import {RegionUsbContext} from './region-usb-context.js';

const failure=code=>Object.assign(Error(code),{code});
const busy=error=>error.code===MANAGEMENT_STATUS.BUSY||error.code==='BUSY';
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const required=[OPERATION.TRACK_READ_BEGIN,OPERATION.TRACK_READ_STATUS,OPERATION.TRACK_READ_CHUNK,OPERATION.TRACK_READ_RELEASE];

// This adapter trusts the proven USB device to authenticate LXMF and pin the
// directory. It does not claim to receive or independently verify LXMF wire.
// Manifest signatures and response/file hashes are still checked in-browser.
export async function createDeviceTrackReadSession({session,directory,author,operations,assertCurrent,timeoutMs=55000,retryMs=100,
  cleanupMs=2000,sleep=delay,now=()=>Date.now(),randomBytes=size=>crypto.getRandomValues(new Uint8Array(size)),regionContext}={}){
 if(!session||typeof session.request!=='function'||!directory?.capabilitiesVerified||!bytes(directory.delivery,16)||!bytes(directory.publicKey,64)||
    !bytes(directory.epoch,16)||!directory.epoch.some(Boolean)||!integer(directory.fileLimit,1,8388608)||!integer(directory.chunkLimit,1,1024)||!integer(directory.queryLimit,1,32)||
    !author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff)||typeof assertCurrent!=='function'||
    !integer(timeoutMs,1,300000)||!integer(retryMs,1,10000)||!integer(cleanupMs,1,30000))throw failure('TRACK_DEVICE_READ_OPTIONS_INVALID');
 if(!Array.isArray(operations)||!required.every(op=>operations.includes(op)))throw failure('FIRMWARE_UNSUPPORTED');
 const delivery=directory.delivery.slice(),epoch=directory.epoch.slice(),key=directory.publicKey.slice(),generation=author.generation;
 const fileLimit=directory.fileLimit,chunkLimit=directory.chunkLimit,pageLimit=directory.queryLimit;
 const identity=await Identity.fromPublicKey(key),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
 const binding=new Uint8Array(26);binding.set(prefix);binding.set(identity.identityHash,10);
 if(!equal(await Identity.truncatedHash(binding),delivery))throw failure('TRACK_DIRECTORY_KEY_MISMATCH');
 assertCurrent();
 const regionBinding=Array.from(new Uint8Array([...delivery,...key,...epoch]),byte=>byte.toString(16).padStart(2,'0')).join('');
 const regions=regionContext??new RegionUsbContext(regionBinding);
 if(!(regions instanceof RegionUsbContext)||regions.binding!==regionBinding)throw failure('TRACK_DIRECTORY_CONTEXT_MISMATCH');
 let pending,closed=false,dirty=false,file;
 async function request(operation,body,{signal}={}){
  if(closed)throw failure('TRACK_DIRECTORY_CLOSED');
  if(dirty)throw failure('TRACK_DEVICE_READ_CLEANUP_REQUIRED');
  if(pending)throw failure('TRACK_DIRECTORY_BUSY');
  if(![0,1,2,8,9,10,11,12,13].includes(operation)||!Array.isArray(body))throw failure('TRACK_DIRECTORY_OPERATION_INVALID');
  assertCurrent();
  const controller=new AbortController();pending=controller;
  const abort=()=>controller.abort(signal.reason??new DOMException('Aborted','AbortError'));
  signal?.addEventListener('abort',abort,{once:true});
  if(signal?.aborted)abort();
  let onAbort;
  const cancelled=new Promise((_,reject)=>{onAbort=()=>reject(controller.signal.reason);controller.signal.addEventListener('abort',onAbort,{once:true});});
  // The cancelled promise is raced below; suppress an early rejection while
  // synchronous input checks run, without changing its rejection semantics.
  cancelled.catch(()=>{});
  const deadline=now()+timeoutMs;
  const timer=setTimeout(()=>controller.abort(failure('TRACK_DIRECTORY_TIMEOUT')),timeoutMs);
  const check=()=>{assertCurrent();if(controller.signal.aborted)throw controller.signal.reason;if(now()>=deadline)throw failure('TRACK_DIRECTORY_TIMEOUT');};
  const call=async(op,parameters)=>{check();const result=await Promise.race([Promise.resolve().then(()=>{check();return session.request(op,parameters);}),cancelled]);check();return result;};
  let id,started=false;
  try{
   check();id=randomBytes(16)?.slice();if(!bytes(id,16)||!id.some(Boolean))throw failure('TRACK_DIRECTORY_REQUEST_ID_INVALID');
   const copied=decode(encode(body),8192);
   let parameters=copied;
   if(operation>=9)parameters=regions.parameters(operation,copied);
   if(operation===8){
    if(copied.length!==3||!bytes(copied[0],32)||!file||!equal(copied[0],file.hash)||!integer(copied[1],0,file.bytes-1)||!integer(copied[2],1,chunkLimit)||copied[2]>file.bytes-copied[1])throw failure('TRACK_FILE_REFERENCE_UNVERIFIED');
    parameters=[copied[0],file.bytes,copied[1],copied[2]];
   }
   const begin=[id.slice(),delivery.slice(),epoch.slice(),generation,operation,parameters];
   if(encode(begin).length>471)throw failure('TRACK_DIRECTORY_REQUEST_TOO_LARGE');
   for(;;){
    try{
     // Set before awaiting: a lost ACK still requires same-ID cleanup.
     started=true;
     const ack=await call(OPERATION.TRACK_READ_BEGIN,begin);
     if(!Array.isArray(ack)||ack.length!==2||!equal(ack[0],id)||ack[1]!==operation)throw failure('TRACK_DEVICE_READ_ACK_INVALID');
     break;
    }catch(error){if(error.code==='TIMEOUT')break;if(!busy(error))throw error;await Promise.race([sleep(retryMs),cancelled]);check();}
   }
   let status;
   for(;;){
    try{status=await call(OPERATION.TRACK_READ_STATUS,[id.slice()]);break;}
    catch(error){if(!busy(error))throw error;await Promise.race([sleep(retryMs),cancelled]);check();}
   }
   if(!Array.isArray(status)||status.length!==5||!equal(status[0],id)||status[1]!==operation||!integer(status[2],200,599)||
      (status[2]!==200&&status[2]<400)||!integer(status[3],1,8192)||!bytes(status[4],32))throw failure('TRACK_DEVICE_READ_STATUS_INVALID');
   const total=status[3],digest=status[4].slice(),raw=new Uint8Array(total);
   for(let offset=0;offset<total;){
    const length=Math.min(320,total-offset),chunk=await call(OPERATION.TRACK_READ_CHUNK,[id.slice(),offset,length]);
    if(!Array.isArray(chunk)||chunk.length!==4||!equal(chunk[0],digest)||chunk[1]!==total||chunk[2]!==offset||!bytes(chunk[3],length))throw failure('TRACK_DEVICE_READ_CHUNK_INVALID');
    raw.set(chunk[3],offset);offset+=length;
   }
   if(!equal(await Identity.fullHash(raw),digest))throw failure('TRACK_DEVICE_READ_HASH_MISMATCH');check();
   const response=decode(raw,8192);
   if(!Array.isArray(response)||response.length!==6||response[0]!==1||response[1]!==1||response[2]!==operation||!equal(response[3],id)||response[4]!==status[2]||!Array.isArray(response[5]))throw failure('TRACK_DIRECTORY_RESPONSE_MISMATCH');
   const result=response[5];
   if(status[2]!==200){if(result.length<1||result.length>2||!validText(result[0],128)||(result.length===2&&!integer(result[1],0,0xffffffff)))throw failure('TRACK_DIRECTORY_RESPONSE_MISMATCH');throw Object.assign(failure('TRACK_DIRECTORY_REMOTE_ERROR'),{status:status[2],diagnostic:result[0]});}
   if(operation===2){
    if(result.length!==2||!integer(result[1],0,1))throw failure('TRACK_MANIFEST_RESPONSE_INVALID');
    const verified=await verifyTrackManifest(result[0]);check();
    if(!equal(verified.objectId,copied[0])||copied[1]!==null&&!equal(verified.revisionHash,copied[1]))throw failure('TRACK_REFERENCE_MISMATCH');
    const [hash,size]=verified.record[13];if(size>fileLimit)throw failure('TRACK_FILE_TOO_LARGE');
    file={hash:hash.slice(),bytes:size};
   }
   if(operation>=9){await regions.learn(operation,copied,result,{signal:controller.signal});check();}
   check();return result;
  }finally{
   clearTimeout(timer);signal?.removeEventListener('abort',abort);controller.signal.removeEventListener('abort',onAbort);
   if(started&&id){
    let cleanupTimer;
    try{
     const cleanup=async()=>{
      const until=now()+cleanupMs;
      for(;;){try{const reply=await session.request(OPERATION.TRACK_READ_RELEASE,[id.slice()]);if(!Array.isArray(reply)||reply.length)throw failure('TRACK_DEVICE_READ_RELEASE_INVALID');return;}
       catch(error){if(error.code===MANAGEMENT_STATUS.EXPIRED)return;if(!busy(error)||now()>=until)throw error;await sleep(retryMs);}}
     };
     await Promise.race([cleanup(),new Promise((_,reject)=>{cleanupTimer=setTimeout(()=>reject(failure('TRACK_DEVICE_READ_CLEANUP_TIMEOUT')),cleanupMs);})]);
    }catch{dirty=true;}finally{clearTimeout(cleanupTimer);}
   }
   if(pending===controller)pending=undefined;
  }
 }
 function close(){closed=true;pending?.abort(failure('TRACK_DIRECTORY_CLOSED'));file=undefined;}
 return Object.freeze({request,close,regionContext:regions,capabilities:Object.freeze({epoch:epoch.slice(),maxFileBytes:fileLimit,chunkBytes:chunkLimit,pageLimit})});
}
