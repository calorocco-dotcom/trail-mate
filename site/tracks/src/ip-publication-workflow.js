import {equal,encode,decode,integer} from '../../geocaching/src/protocol.js';
import {withIPPublicationLock} from '../../geocaching/src/ip-publication-journal.js';
import {readTrackDraft,saveTrackDraftPublicationPlan} from './draft-store.js';
import {validateTrackPublicationResume,decodeTrackPublicationPlan} from './publication-plan.js';
import {createTrackPublicationPlan} from './publication-create.js';
import {createTrackSuccessorPlan} from './publication-successor.js';
import {loadTrackIPCheckpoint,loadPreviousTrackIPCheckpoint,saveTrackIPCheckpoint,encodeTrackIPCheckpoint,decodeTrackIPCheckpoint} from './ip-publication-journal.js';
import {signDeviceTrackManifest} from './device-local-signature.js';
import {verifyTrackReceipt} from './receipt.js';
import {verifyTrackManifest} from './manifest.js';
import {withTrackDraftLock} from './draft-lock.js';
import {draftPackageCreationOptions,draftPackageInputs} from './draft-package.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');

export async function reviewTrackIPPublication(draftId,draftVersion,author,directory,network,options={}){
 const account={...author,publicKey:author.publicKey?.slice()},target={...directory,publicKey:directory?.publicKey?.slice(),delivery:directory?.delivery?.slice(),epoch:directory?.epoch?.slice()};
 return withTrackDraftLock(draftId,()=>withIPPublicationLock(account.publicKey,async()=>{
  if(!account.verified||!integer(account.generation,1,0xffffffff)||!target.capabilitiesVerified||!network||network.closed)throw Error('Verified track publication identity and directory required');
  const entry=network.directories.get(target.key);
  if(!entry?.ready||!equal(entry.epoch,target.epoch)||!equal(entry.destination,target.delivery)||!equal(await entry.identity.getPublicKey(),target.publicKey)||target.key!==hex(target.delivery))throw Error('Track publication directory changed');
  const previous=loadTrackIPCheckpoint(account.publicKey,draftId,draftVersion,options);
  if(previous){
   if(!previous.signature&&previous.plan.generation!==account.generation)throw Error('Track device identity changed');
   if(!equal(previous.directoryKey,target.publicKey)||!equal(previous.epoch,target.epoch)||!equal(previous.delivery,target.delivery))throw Error('Retained track publication targets another directory');
   if(previous.stage==='confirmed')await verifyTrackReceipt(decode(previous.receipt,1200),{directoryPublicKey:previous.directoryKey,directoryEpoch:previous.epoch,requestId:previous.requestId,manifestSigned:[previous.plan.manifest,previous.signature]});
   else if(previous.signature)await verifyTrackManifest([previous.plan.manifest,previous.signature],{expectedAuthor:account.publicKey});
   return previous;
  }
  let snapshot=await (options.readDraft??readTrackDraft)(draftId);
  if(snapshot.version!==draftVersion)throw Error('Track draft changed');
  if(!snapshot.publicationPlan){
   const published=loadPreviousTrackIPCheckpoint(account.publicKey,draftId,draftVersion,options);
   if(published){
    const signed=[published.plan.manifest,published.signature];
    await verifyTrackManifest(signed,{expectedAuthor:account.publicKey});
    await verifyTrackReceipt(decode(published.receipt,1200),{directoryPublicKey:published.directoryKey,directoryEpoch:published.epoch,requestId:published.requestId,manifestSigned:signed});
    snapshot={...snapshot,predecessor:encode(signed).buffer};
   }
  }
  const fileHash=new Uint8Array(await crypto.subtle.digest('SHA-256',snapshot.source));let plan;
  const packageOptions=await draftPackageCreationOptions(snapshot);
  if(snapshot.publicationPlan)plan=validateTrackPublicationResume(decodeTrackPublicationPlan(new Uint8Array(snapshot.publicationPlan)),account,fileHash,snapshot.source.length);
  else if(snapshot.predecessor)plan=await createTrackSuccessorPlan(snapshot.source,account,decode(new Uint8Array(snapshot.predecessor),4352),{...options.creationOptions,...packageOptions,title:snapshot.name,archive:snapshot.archive===true,signal:options.signal});
  else plan=await createTrackPublicationPlan(snapshot.source,account,{...options.creationOptions,...packageOptions,title:snapshot.name});
  const fields=decode(plan.manifest,4096);
  if(fields[0]===2&&fields[15]&&!entry.operations?.includes(14))throw Object.assign(Error('Selected directory cannot publish attachments'),{code:'TRACK_PACKAGE_DIRECTORY'});
  if(options.signal?.aborted)throw options.signal.reason??new DOMException('Paused','AbortError');
  await (options.savePlan??saveTrackDraftPublicationPlan)(draftId,draftVersion,plan);
  if(options.signal?.aborted)throw options.signal.reason??new DOMException('Paused','AbortError');
  if(network.closed||network.directories.get(target.key)!==entry||!entry.ready||!equal(entry.epoch,target.epoch))throw Error('Track publication directory changed');
  const requestId=(options.randomBytes??(size=>crypto.getRandomValues(new Uint8Array(size))))(16);
  return saveTrackIPCheckpoint({stage:'reviewed',draftId,draftVersion,authorKey:account.publicKey,requestId,directoryKey:target.publicKey,delivery:target.delivery,epoch:target.epoch,plan,signature:null,receipt:null},options);
 },options),options);
}

export async function signTrackIPPublication(session,record,author,options={}){
 const frozen=decodeTrackIPCheckpoint(encodeTrackIPCheckpoint(record)),account={...author,publicKey:author.publicKey?.slice()};
 return withTrackDraftLock(frozen.draftId,()=>withIPPublicationLock(frozen.authorKey,async()=>{
  const current=loadTrackIPCheckpoint(frozen.authorKey,frozen.draftId,frozen.draftVersion,options);
  if(!current||!equal(encodeTrackIPCheckpoint(current),encodeTrackIPCheckpoint(frozen)))throw Error('Track publication checkpoint changed');
  if(!['reviewed','signing'].includes(current.stage))throw Error('Track publication is not awaiting signature');
  const draft=await (options.readDraft??readTrackDraft)(current.draftId);
  if(draft.version!==current.draftVersion)throw Error('Track draft changed');
  const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',draft.source));
  validateTrackPublicationResume(current.plan,account,hash,draft.source.length);
  const signing=saveTrackIPCheckpoint({...current,stage:'signing'},{...options,expected:current});
  const attachments=await draftPackageInputs(draft,signing.plan);
  const result=await signDeviceTrackManifest(session,{...signing.plan,...attachments,kml:draft.source,author:account},options);
  if(!equal(result.signed[0],signing.plan.manifest))throw Error('Device signed different track publication');
  return saveTrackIPCheckpoint({...signing,stage:'signed',signature:result.signed[1]},{...options,expected:signing});
 },options),options);
}

export async function publishTrackIPPublication(network,record,author,options={}){
 const frozen=decodeTrackIPCheckpoint(encodeTrackIPCheckpoint(record)),account={...author,publicKey:author.publicKey?.slice()};
 return withTrackDraftLock(frozen.draftId,()=>withIPPublicationLock(frozen.authorKey,async()=>{
  const current=loadTrackIPCheckpoint(frozen.authorKey,frozen.draftId,frozen.draftVersion,options);
  if(!current||!equal(encodeTrackIPCheckpoint(current),encodeTrackIPCheckpoint(frozen)))throw Error('Track publication checkpoint changed');
  if(!account.verified||!integer(account.generation,1,0xffffffff)||!equal(account.publicKey,current.authorKey))throw Error('Track device identity changed');
  const signed=[current.plan.manifest.slice(),current.signature?.slice()];
  const context={directoryPublicKey:current.directoryKey,directoryEpoch:current.epoch,requestId:current.requestId,manifestSigned:signed};
  if(current.stage==='confirmed'){await verifyTrackReceipt(decode(current.receipt,1200),context);return current;}
  if(!['signed','publishing','unconfirmed'].includes(current.stage)||!network||network.closed||typeof network.publishTrack!=='function')throw Error('Track publication is not ready for IP network');
  await verifyTrackManifest(signed,{expectedAuthor:account.publicKey});
  const key=hex(current.delivery),entry=network.directories.get(key);
  if(!entry?.ready||!equal(entry.epoch,current.epoch)||!equal(await entry.identity.getPublicKey(),current.directoryKey))throw Error('Track publication directory changed');
  const draft=await (options.readDraft??readTrackDraft)(current.draftId);
  if(draft.version!==current.draftVersion)throw Error('Track draft changed');
  if(!equal(new Uint8Array(await crypto.subtle.digest('SHA-256',draft.source)),current.plan.fileHash)||draft.source.length!==current.plan.fileBytes)throw Error('Track draft changed');
  const publishing=saveTrackIPCheckpoint({...current,stage:'publishing'},{...options,expected:current});
  let proof;
  try{
   const attachments=await draftPackageInputs(draft,current.plan,{persist:false});
   const result=await network.publishTrack(key,signed,draft.source,{...attachments,authorPublicKey:account.publicKey,requestId:publishing.requestId,signal:options.signal,onProgress:options.onProgress});
   if(result?.directoryConfirmed!==true)throw Error('Track directory publication unconfirmed');
   await verifyTrackReceipt(result.signed,context);proof=encode(result.signed);
  }catch(error){saveTrackIPCheckpoint({...publishing,stage:'unconfirmed'},{...options,expected:publishing});throw error;}
  return saveTrackIPCheckpoint({...publishing,stage:'confirmed',receipt:proof},{...options,expected:publishing});
 },options),options);
}
