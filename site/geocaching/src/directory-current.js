import {toHex} from '@reticulum/core';
import {bytes,verifySigned} from './protocol.js';
export async function readDirectoryCurrent(client,directoryKey,cacheId){
 if(!bytes(cacheId,32))throw Error('Invalid cache ID');
 const id=cacheId.slice(),directory=client.directories.get(directoryKey);
 if(!directory?.ready)throw Error('Verified directory unavailable');
 return client.enqueue(async()=>{
  const current=()=>!client.closed&&client.directories.get(directoryKey)===directory&&directory.ready;
  if(!current())throw Error('Directory connection changed');
  let reply;
  try{reply=await client.request(directory,3,[id,null,null]);}
  catch(error){if(current()&&error.status===404&&error.reason==='not_found')return {exists:false};throw error;}
  if(!current())throw Error('Directory connection changed');
  if(!Array.isArray(reply)||reply.length!==3||reply[1]!==1||reply[2]!==0)throw Error('Directory current version is ambiguous');
  const verified=await verifySigned(reply[0]);
  if(verified.cacheId!==toHex(id))throw Error('Directory returned a different cache');
  return {exists:true,signed:verified.signed,verified};
 });
}
