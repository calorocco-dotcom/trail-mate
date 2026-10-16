import {bytes,equal} from '../../geocaching/src/protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {validateAuthorInfo} from '../../device/src/author-identity.js';
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
// Fresh signed/index/resource/actual-route disk proof; no upload IDs or stored
// success cache are used. A missing .kml is distinct from corrupt proof/bytes.
export async function queryInstalledDevicePackage(session,{author,operationId,objectId,revisionHash},{operations=[],signal,timeoutMs=900000,retryMs=500,sleep=delay,now=()=>performance.now()}={}){
 if(!Array.isArray(operations)||!operations.includes(101)||!session||typeof session.request!=='function'||!author?.verified||!bytes(author.publicKey,64)||!Number.isInteger(author.generation)||author.generation<1||author.generation>0xffffffff||
  !bytes(operationId,16)||!operationId.some(Boolean)||!bytes(objectId,32)||!objectId.some(Boolean)||!bytes(revisionHash,32)||!revisionHash.some(Boolean))throw Error('Invalid installed package query');
 if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0||retryMs>=30000||typeof sleep!=='function'||typeof now!=='function')throw Error('Invalid package query timing');
 const key=author.publicKey.slice(),generation=author.generation,operation=operationId.slice(),object=objectId.slice(),revision=revisionHash.slice();
 const target='/routes/p-'+Array.from(revision,byte=>byte.toString(16).padStart(2,'0')).join('')+'.kml',deadline=now()+timeoutMs;
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Package query paused','AbortError');if(now()>=deadline)throw Error('Package query unconfirmed');};check();
 const current=await validateAuthorInfo(await session.request(OPERATION.AUTHOR_INFO,[]));check();
 if(current.generation!==generation||!equal(current.publicKey,key))throw Object.assign(Error('Device identity changed'),{code:MANAGEMENT_STATUS.STALE_IDENTITY});
 for(;;){
  check();let reply,busy=false;
  try{reply=await session.request(OPERATION.TRACK_PACKAGE_INSTALL_STATUS,[operation.slice(),generation,object.slice(),revision.slice()]);}
  catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;busy=true;}
  check();
  if(!busy){
   if(!Array.isArray(reply)||reply.length!==5||!bytes(reply[0],16)||!equal(reply[0],operation)||![0,1,2].includes(reply[1]))throw Error('Invalid installed package response');
   if(reply[1]!==0){if(!bytes(reply[2],32)||!equal(reply[2],object)||!bytes(reply[3],32)||!equal(reply[3],revision)||reply[4]!==target)throw Error('Installed package response binding mismatch');return{installed:reply[1]===1,missing:reply[1]===2,objectId:object,revisionHash:revision,target,generation};}
   if(reply.slice(2).some(value=>value!==null))throw Error('Invalid pending package query');
  }
  await sleep(Math.min(retryMs,Math.max(1,deadline-now())));
 }
}
