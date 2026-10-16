import {Identity} from '@reticulum/core';
import {LXMessage} from '@reticulum/lxmf';
import {bytes,integer,equal,encode,decode} from '../../geocaching/src/protocol.js';

export const TRACK_DIRECTORY_APP='trailmate.track.v1';
const failure=code=>Object.assign(Error(code),{code});
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};

// exchange sends the fields through the selected authenticated transport and
// returns full LXMF wire bytes. The response is independently authenticated here.
export async function createTrackDirectoryReadSession({directoryPublicKey,directoryDelivery,localDelivery,exchange,timeoutMs=45000,signal,metadataQueries=false}={}){
 check(signal);
 if(!bytes(directoryPublicKey,64)||!bytes(directoryDelivery,16)||!bytes(localDelivery,16)||!localDelivery.some(Boolean)||typeof exchange!=='function'||!integer(timeoutMs,1,300000))throw failure('TRACK_DIRECTORY_OPTIONS_INVALID');
 const key=directoryPublicKey.slice(),directory=directoryDelivery.slice(),local=localDelivery.slice();
 const identity=await Identity.fromPublicKey(key);
 const name=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
 const binding=new Uint8Array(26);binding.set(name);binding.set(identity.identityHash,10);
 if(!equal(await Identity.truncatedHash(binding),directory))throw failure('TRACK_DIRECTORY_KEY_MISMATCH');
 check(signal);
 const prefix=crypto.getRandomValues(new Uint8Array(8));let sequence=0n,pending,closed=false,metadataOperations=[];
 async function request(operation,body,{signal:cancel}={}){
   check(cancel);
   if(closed)throw failure('TRACK_DIRECTORY_CLOSED');
   if(pending)throw failure('TRACK_DIRECTORY_BUSY');
   if(![0,1,2,8,9,10,11,12,13,15,16].includes(operation)||!Array.isArray(body))throw failure('TRACK_DIRECTORY_OPERATION_INVALID');
   if([15,16].includes(operation)&&!metadataOperations.includes(operation))throw failure('TRACK_QUERY_METADATA_UNSUPPORTED');
   if(++sequence>0xffffffffffffffffn)throw failure('TRACK_DIRECTORY_SEQUENCE_EXHAUSTED');
   const id=new Uint8Array(16);id.set(prefix);new DataView(id.buffer).setBigUint64(8,sequence);
   const payload=encode([1,0,operation,id,8192,body]);
   if(payload.length>8192)throw failure('TRACK_DIRECTORY_REQUEST_TOO_LARGE');
   const controller=new AbortController();pending=controller;
   const abort=()=>controller.abort(cancel.reason??new DOMException('Aborted','AbortError'));
   cancel?.addEventListener('abort',abort,{once:true});
   let onAbort;
   const cancelled=new Promise((_,reject)=>{onAbort=()=>reject(controller.signal.reason);controller.signal.addEventListener('abort',onAbort,{once:true});});
   const timer=setTimeout(()=>controller.abort(failure('TRACK_DIRECTORY_TIMEOUT')),timeoutMs);
   try{
     check(cancel);check(controller.signal);
     const wire=await Promise.race([Promise.resolve().then(()=>{check(controller.signal);return exchange({directoryDelivery:directory.slice(),localDelivery:local.slice(),fields:new Map([[0xfb,TRACK_DIRECTORY_APP],[0xfc,payload]]),signal:controller.signal});}),cancelled]);
     check(controller.signal);
     if(!(wire instanceof Uint8Array)||wire.length<96||wire.length>16384)throw failure('TRACK_DIRECTORY_WIRE_INVALID');
     const message=await LXMessage.deserialize(wire.slice(),local);
     check(controller.signal);
     if(!equal(message.sourceHash,directory)||!equal(message.destinationHash,local)||!await message.verifySignature(identity))throw failure('TRACK_DIRECTORY_AUTH_INVALID');
     check(controller.signal);
     const field=number=>message.fields instanceof Map?message.fields.get(number):message.fields?.[number];
     if(field(0xfb)!==TRACK_DIRECTORY_APP)throw failure('TRACK_DIRECTORY_APP_MISMATCH');
     const response=decode(field(0xfc),8192);
     if(!Array.isArray(response)||response.length!==6||response[0]!==1||response[1]!==1||response[2]!==operation||!equal(response[3],id)||!Array.isArray(response[5]))throw failure('TRACK_DIRECTORY_RESPONSE_MISMATCH');
     if(response[4]!==200){if(!integer(response[4],400,599))throw failure('TRACK_DIRECTORY_RESPONSE_MISMATCH');throw Object.assign(failure('TRACK_DIRECTORY_REMOTE_ERROR'),{status:response[4],diagnostic:String(response[5][0]??'').slice(0,128)});}
     return response[5];
   }finally{clearTimeout(timer);cancel?.removeEventListener('abort',abort);controller.signal.removeEventListener('abort',onAbort);if(pending===controller)pending=undefined;}
 }
 function close(){closed=true;pending?.abort(failure('TRACK_DIRECTORY_CLOSED'));}
 try{
   const caps=await request(0,[],{signal});
   if(caps.length!==8||caps[0]!==1||caps[1]!==TRACK_DIRECTORY_APP||!bytes(caps[2],64)||!equal(caps[2],key)||!bytes(caps[3],16)||!caps[3].some(Boolean)||!integer(caps[4],1,8388608)||!integer(caps[5],1,1024)||!integer(caps[6],1,32)||!Array.isArray(caps[7])||![0,1,2,8].every(op=>caps[7].includes(op)))throw failure('TRACK_DIRECTORY_CAPABILITIES_INVALID');
   metadataOperations=caps[7].filter(op=>[15,16].includes(op));
   if(metadataQueries){let extended;try{extended=await request(0,[2],{signal});}catch(error){if(error.status!==422)throw error;}
    if(extended!==undefined){if(extended.length!==8||extended[0]!==caps[0]||extended[1]!==caps[1]||!equal(extended[2],key)||!equal(extended[3],caps[3])||extended[4]!==caps[4]||extended[5]!==caps[5]||extended[6]!==caps[6]||!Array.isArray(extended[7])||![0,1,2,8].every(op=>extended[7].includes(op)))throw failure('TRACK_DIRECTORY_CAPABILITIES_INVALID');metadataOperations=extended[7].filter(op=>[15,16].includes(op));}
   }
   check(signal);
   return Object.freeze({request,close,capabilities:Object.freeze({epoch:caps[3].slice(),maxFileBytes:caps[4],chunkBytes:caps[5],pageLimit:caps[6],metadataSupported:metadataOperations.includes(15),metadataRegionSupported:metadataOperations.includes(16)})});
 }catch(error){close();throw error;}
}
