import {bytes,equal,verifySigned} from './protocol.js';

// Content authorship is the device signature. LXMF transport uses the
// browser's existing IP session and never requires the device's RF backend.
export async function publishSignedCache(client,directoryKey,signed,authorPublicKey){
 if(!bytes(authorPublicKey,64)||!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64))throw Error('Invalid signed publication');
 const owned=[signed[0].slice(),signed[1].slice()],expectedAuthor=authorPublicKey.slice();
 const verified=await verifySigned(owned);
 if(!equal(verified.record[1],expectedAuthor))throw Error('Publication author differs from connected device');
 const directory=client.directories.get(directoryKey);
 if(!directory?.ready)throw Error('Verified directory connection unavailable');
 return client.enqueue(async()=>{
  if(client.closed||client.directories.get(directoryKey)!==directory||!directory.ready)throw Error('Verified directory connection changed');
  let receipt;
  try{receipt=await client.request(directory,1,[owned]);}
  catch(error){throw Object.assign(Error(error.message),{code:'PUBLICATION_UNCONFIRMED',cause:error});}
  const summary=verified.summary;
  if(!Array.isArray(receipt)||receipt.length!==7||!equal(receipt[0],summary[0])||receipt[1]!==summary[1]||!equal(receipt[2],summary[2])||
     ![0,1].includes(receipt[3])||receipt[4]!==summary[1]||!equal(receipt[5],summary[2])||receipt[6]!==summary[3])
   throw Object.assign(Error('Directory receipt differs from signed publication'),{code:'PUBLICATION_UNCONFIRMED'});
  return {cacheId:verified.cacheId,revision:summary[1],revisionHash:verified.revisionHash,state:summary[3],duplicate:receipt[3]===1,confirmed:true};
 });
}
