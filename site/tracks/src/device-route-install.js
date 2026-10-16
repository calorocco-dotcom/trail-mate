import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {OPERATION} from '../../device/src/management-protocol.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
export function installedRoutePath(hash){
 if(!bytes(hash,32))fail('ROUTE_HASH_INVALID');
 return '/routes/'+Array.from(hash,byte=>byte.toString(16).padStart(2,'0')).join('')+'.kml';
}
// This consumes a sealed file-upload receipt, not an arbitrary SD pathname.
// The device must independently enforce kind 3, owner, hash, profile and SD state.
export async function installUploadedRoute(session,upload,author,{signal}={}){
 if(!session||typeof session.request!=='function'||!upload?.sealed||upload.published!==false||!bytes(upload.id,16)||!upload.id.some(byte=>byte)||!bytes(upload.sha256,32)||!integer(upload.bytes,1,8388608))fail('ROUTE_UPLOAD_INVALID');
 if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xfffffffe))fail('ROUTE_AUTHOR_INVALID');
 if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');
 const id=upload.id.slice(),hash=upload.sha256.slice(),authorKey=author.publicKey.slice(),total=upload.bytes,generation=author.generation,path=installedRoutePath(hash);
 let reply;
 try{reply=await session.request(OPERATION.ROUTE_INSTALL,[id.slice(),hash.slice(),total,generation]);}
 catch(error){
  // Explicit protocol rejection confirms no install. Transport/internal failure
  // leaves the result unknown; callers must retain this binding and query later.
  if(Number.isInteger(error.code)&&error.code>=1&&error.code<=10)throw error;
  throw Object.assign(Error('ROUTE_INSTALL_UNCONFIRMED'),{code:'ROUTE_INSTALL_UNCONFIRMED',cause:error,id,sha256:hash,bytes:total,path,authorKey});
 }
 if(signal?.aborted||!Array.isArray(reply)||reply.length!==5||!bytes(reply[0],16)||!equal(reply[0],id)||!bytes(reply[1],32)||!equal(reply[1],hash)||reply[2]!==total||reply[3]!==path||!integer(reply[4],0,1))
  throw Object.assign(Error('ROUTE_INSTALL_UNCONFIRMED'),{code:'ROUTE_INSTALL_UNCONFIRMED',id,sha256:hash,bytes:total,path,authorKey});
 return {id,sha256:hash,bytes:total,path,authorKey,installed:reply[4]===1,pending:reply[4]===0};
}
// Read-only confirmation may be repeated after reconnect. It never resubmits
// the install operation, including when the target is confirmed missing.
export async function queryRouteInstallation(session,binding,author,{signal}={}){
 if(!session||typeof session.request!=='function'||!bytes(binding?.id,16)||!binding.id.some(byte=>byte)||!bytes(binding.sha256,32)||!integer(binding.bytes,1,8388608))fail('ROUTE_UPLOAD_INVALID');
 if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xfffffffe))fail('ROUTE_AUTHOR_INVALID');
 if(binding.authorKey!==undefined&&(!bytes(binding.authorKey,64)||!equal(binding.authorKey,author.publicKey)))fail('ROUTE_AUTHOR_CHANGED');
 if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');
 const id=binding.id.slice(),hash=binding.sha256.slice(),authorKey=author.publicKey.slice(),total=binding.bytes,path=installedRoutePath(hash);
 const reply=await session.request(OPERATION.ROUTE_INSTALL_STATUS,[id.slice(),hash.slice(),total,author.generation]);
 if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');
 if(!Array.isArray(reply)||reply.length!==5||!bytes(reply[0],16)||!equal(reply[0],id)||!bytes(reply[1],32)||!equal(reply[1],hash)||reply[2]!==total||reply[3]!==path||!integer(reply[4],0,2))fail('ROUTE_STATUS_INVALID');
 return {id,sha256:hash,bytes:total,path,authorKey,pending:reply[4]===0,installed:reply[4]===1,missing:reply[4]===2};
}
