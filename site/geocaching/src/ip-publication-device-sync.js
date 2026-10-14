import {equal,integer} from './protocol.js';
import {verifyPublicationReceipt} from './publication-receipt.js';
import {loadIPPublication,saveIPPublication,withIPPublicationLock} from './ip-publication-journal.js';

export async function syncIPPublicationToDevice(reader,record,author,{operations,...options}={}){
 const expected={...record,authorKey:record.authorKey.slice(),operationId:record.operationId.slice(),hash:record.hash.slice()};
 const account={...author,publicKey:author.publicKey?.slice()};operations=operations?.slice();
 return withIPPublicationLock(expected.authorKey,async()=>{
  const stored=loadIPPublication(expected.authorKey,options);
  if(!stored||stored.stage!=='confirmed'||!equal(stored.operationId,expected.operationId)||!equal(stored.hash,expected.hash))
   throw Object.assign(Error('Publication checkpoint changed'),{code:'PUBLICATION_CHECKPOINT_STALE'});
  if(!account.verified||!equal(stored.authorKey,account.publicKey)||!integer(account.generation,1,0xffffffff))throw Error('Publication device identity changed');
  if(!stored.receipt)throw Error('Retain directory proof before device synchronization');
  await verifyPublicationReceipt([stored.receipt.raw,stored.receipt.signature],[stored.raw,stored.signature],stored.receipt);
  const sameGeneration=(stored.deviceSync?.generation??stored.authorGeneration)===account.generation;
  if(stored.deviceSync?.stage==='confirmed'&&sameGeneration)return stored;
  if(![32,33,35,75,76].every(op=>operations?.includes(op)))throw Error('Device confirmation capability unavailable');
  const id=(sameGeneration?stored.deviceSync?.id.slice():null)??(options.randomBytes??(size=>crypto.getRandomValues(new Uint8Array(size))))(16);
  const pending=saveIPPublication({...stored,deviceSync:{stage:'pending',id,generation:account.generation}},{...options,expected:stored});
  const result=await reader.confirmPublication([pending.raw.slice(),pending.signature.slice()],pending.receipt,account,
   {transferId:pending.deviceSync.id.slice(),operations,signal:options.signal});
  if(result?.confirmed!==true)throw Error('Device synchronization is not durably confirmed');
  return saveIPPublication({...pending,deviceSync:{stage:'confirmed',id:pending.deviceSync.id,generation:account.generation}},{...options,expected:pending});
 },options);
}
