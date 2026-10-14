import {bytes,equal,verifySigned} from './protocol.js';
import {verifyPublicationReceipt} from './publication-receipt.js';

export async function readDirectoryPublicationReceipt(client,directoryKey,signed,authorPublicKey){
 if(!bytes(authorPublicKey,64)||!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64))throw Error('Invalid receipt content');
 const content=signed.map(part=>part.slice()),author=authorPublicKey.slice(),verified=await verifySigned(content);
 if(!equal(verified.record[1],author))throw Error('Receipt content author mismatch');
 const directory=client.directories.get(directoryKey);
 if(!directory?.ready)throw Error('Verified directory unavailable');
 if(directory.receiptSupported!==true)throw Object.assign(Error('Directory receipt proof unsupported'),{code:'DIRECTORY_RECEIPT_UNSUPPORTED'});
 return client.enqueue(async()=>{
  if(client.closed||client.directories.get(directoryKey)!==directory||!directory.ready)throw Error('Directory connection changed');
  const entry={publicKey:(await directory.identity.getPublicKey()).slice(),delivery:directory.destination.slice(),epoch:directory.epoch.slice()};
  if(client.closed||client.directories.get(directoryKey)!==directory||!directory.ready)throw Error('Directory connection changed');
  const proof=await client.request(directory,5,[verified.summary[0].slice(),verified.summary[2].slice(),author.slice()]);
  return verifyPublicationReceipt(proof,content,entry);
 });
}
