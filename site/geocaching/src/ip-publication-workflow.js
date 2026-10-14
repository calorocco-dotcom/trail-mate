import {equal,integer,verifySigned} from './protocol.js';
import {buildPublicationRecord,deviceCacheId} from './publication-record.js';
import {loadIPPublication,saveIPPublication,withIPPublicationLock} from './ip-publication-journal.js';
import {verifyPublicationReceipt} from './publication-receipt.js';
const copyProof=proof=>proof?Object.fromEntries(['raw','signature','publicKey','delivery','epoch'].map(key=>[key,proof[key].slice()])):undefined;
const copyRecord=record=>({...record,authorKey:record.authorKey.slice(),draftId:record.draftId.slice(),operationId:record.operationId.slice(),
  transferId:record.transferId.slice(),hash:record.hash.slice(),raw:record.raw.slice(),signature:record.signature?.slice()??null,...(record.receipt?{receipt:copyProof(record.receipt)}:{})});
function identity(record,author){
 if(!author?.verified||!equal(record.authorKey,author.publicKey)||record.authorGeneration!==author.generation)throw Error('Publication device identity changed');
}
function current(record,options){
 const stored=loadIPPublication(record.authorKey,options);
 if(!stored||!equal(stored.operationId,record.operationId)||!equal(stored.hash,record.hash)||stored.stage!==record.stage)throw Object.assign(Error('Publication checkpoint changed'),{code:'PUBLICATION_CHECKPOINT_STALE'});
 return stored;
}
export async function reviewIPPublication(reader,snapshot,author,directory,network,options={}){
 author={...author,publicKey:author.publicKey.slice()};directory={...directory};
 snapshot={...snapshot,draft:{...snapshot.draft,id:snapshot.draft.id.slice(),authorKey:snapshot.draft.authorKey?.slice()??null,baseHash:snapshot.draft.baseHash?.slice()??null}};
 return withIPPublicationLock(author.publicKey,async()=>{
  if(!author.verified||!integer(author.generation,1,0xffffffff))throw Error('Publication device identity changed');
  const previous=loadIPPublication(author.publicKey,options);
  if(previous&&previous.stage!=='confirmed')throw Object.assign(Error('Resolve retained publication first'),{code:'PUBLICATION_RETAINED'});
  if(!directory?.capabilitiesVerified||!network?.ready())throw Error('Verified browser directory unavailable');
  if(previous){
   await verifySigned([previous.raw,previous.signature]);
   // Old-generation uploads belong to the previous device incarnation. New
   // reviews must not try to delete them using the restored account's lease.
   if(previous.authorGeneration===author.generation)await reader.clearSignatureTransfer(previous.transferId,author);
  }
  const cacheId=await deviceCacheId(author.publicKey,snapshot.draft.id),head=await network.current(cacheId,directory.key);
  const publication=await buildPublicationRecord(snapshot.draft,author,head,options);
  const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',publication.raw)),random=options.randomBytes??(size=>crypto.getRandomValues(new Uint8Array(size)));
  return saveIPPublication({stage:'reviewed',authorKey:author.publicKey.slice(),authorGeneration:author.generation,draftId:snapshot.draft.id.slice(),draftGeneration:snapshot.draft.generation,
    operationId:random(16),transferId:random(16),hash,raw:publication.raw,signature:null,directoryKey:directory.key},options);
 },options);
}
export async function signIPPublication(reader,record,author,{operations,...options}={}){
 record=copyRecord(record);author={...author,publicKey:author.publicKey.slice()};operations=operations?.slice();
 return withIPPublicationLock(record.authorKey,async()=>{
  identity(record,author);const stored=current(record,options);
  if(!['reviewed','signing'].includes(stored.stage))throw Error('Publication is not awaiting signature');
  const signing=saveIPPublication({...stored,stage:'signing'},{...options,expected:stored});
  const result=await reader.signRecord(signing.raw,author,{transferId:signing.transferId,operationId:signing.operationId,draftId:signing.draftId,draftGeneration:signing.draftGeneration,operations,signal:options.signal});
  const verified=await verifySigned(result.signed);
  if(!equal(result.signed[0],signing.raw)||!equal(verified.record[1],signing.authorKey))throw Error('Device signed different publication content');
  return saveIPPublication({...signing,stage:'signed',signature:result.signed[1].slice()},{...options,expected:signing});
 },options);
}
export async function publishIPPublication(reader,record,author,network,options={}){
 record=copyRecord(record);author={...author,publicKey:author.publicKey.slice()};
 return withIPPublicationLock(record.authorKey,async()=>{
  if(!author.verified||!equal(record.authorKey,author.publicKey)||!integer(author.generation,1,0xffffffff))throw Error('Publication device identity changed');
  const stored=current(record,options);
  if(!['signed','publishing','unconfirmed'].includes(stored.stage)||!network?.ready())throw Error('Publication is not ready for browser network');
  const verified=await verifySigned([stored.raw,stored.signature]);
  const publishing=saveIPPublication({...stored,stage:'publishing'},{...options,expected:stored});
  let receipt;
  try{
    receipt=await network.publish([publishing.raw,publishing.signature],author.publicKey,publishing.directoryKey);
    if(receipt?.confirmed!==true||receipt.cacheId!==verified.cacheId||receipt.revision!==verified.record[3]||receipt.revisionHash!==verified.revisionHash||receipt.state!==verified.record[5])throw Error('Publication confirmation binding mismatch');
  }
  catch(error){saveIPPublication({...publishing,stage:'unconfirmed'},{...options,expected:publishing});throw error;}
  const confirmed=saveIPPublication({...publishing,stage:'confirmed'},{...options,expected:publishing});
  let cleanupError=null;
  if(confirmed.authorGeneration===author.generation){try{await reader.clearSignatureTransfer(confirmed.transferId,author);}catch(error){cleanupError=error.message;}}
  return {record:confirmed,receipt,cleanupError};
 },options);
}

export async function retainIPPublicationReceipt(record,author,network,options={}){
 record=copyRecord(record);author={...author,publicKey:author.publicKey.slice()};
 return withIPPublicationLock(record.authorKey,async()=>{
  if(!author.verified||!equal(record.authorKey,author.publicKey)||!integer(author.generation,1,0xffffffff))throw Error('Publication device identity changed');
  const stored=current(record,options);
  if(stored.stage!=='confirmed')throw Error('Directory proof requires a confirmed publication');
  let receipt=stored.receipt;
  if(!receipt){
   if(!network?.ready()||typeof network.publicationReceipt!=='function')throw Error('Directory receipt connection unavailable');
   const result=await network.publicationReceipt([stored.raw.slice(),stored.signature.slice()],author.publicKey.slice(),stored.directoryKey);
   if(!result?.proof||!Array.isArray(result.proof))throw Error('Invalid directory proof response');
   receipt=copyProof({raw:result.proof[0],signature:result.proof[1],publicKey:result.publicKey,delivery:result.delivery,epoch:result.epoch});
  }
  await verifyPublicationReceipt([receipt.raw,receipt.signature],[stored.raw,stored.signature],receipt);
  const key=Array.from(receipt.delivery,b=>b.toString(16).padStart(2,'0')).join('');
  if(key!==stored.directoryKey)throw Error('Directory receipt target changed');
  if(stored.receipt)return stored;
  return saveIPPublication({...stored,receipt},{...options,expected:stored});
 },options);
}
