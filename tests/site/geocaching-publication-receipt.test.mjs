import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,verifySigned} from '../../site/geocaching/src/protocol.js';
import {verifyPublicationReceipt} from '../../site/geocaching/src/publication-receipt.js';
import {readDirectoryPublicationReceipt} from '../../site/geocaching/src/directory-receipt.js';
import {retainIPPublicationReceipt} from '../../site/geocaching/src/ip-publication-workflow.js';
import {saveIPPublication,loadIPPublication} from '../../site/geocaching/src/ip-publication-journal.js';
import {uploadDevicePublicationConfirmation} from '../../site/geocaching/src/device-confirmation.js';
import {beginDevicePublicationConfirmation,readDevicePublicationConfirmation} from '../../site/geocaching/src/device-confirmation-status.js';
import {DeviceDraftReader} from '../../site/geocaching/src/device-draft-reader.js';
import {syncIPPublicationToDevice} from '../../site/geocaching/src/ip-publication-device-sync.js';
const text=new TextEncoder(),join=(...parts)=>Uint8Array.from(parts.flatMap(part=>[...part]));
test('portable directory proof binds signer, destination, epoch and exact signed head',async()=>{
 const author=await Identity.generate(),directory=await Identity.generate(),pub=await directory.getPublicKey();
 const raw=encode([1,await author.getPublicKey(),new Uint8Array(16).fill(1),1,null,0,1,1,'Proof','','',2,2,0,1800000000,1800000000]);
 const signed=[raw,await author.sign(join(text.encode('trailmate.geocache/sign/v1\0'),raw))],verified=await verifySigned(signed);
 const delivery=await Identity.truncatedHash(join((await Identity.fullHash(text.encode('lxmf.delivery'))).slice(0,10),directory.identityHash)),epoch=new Uint8Array(16).fill(2);
 const fields=[1,delivery,epoch,verified.summary[0],1,verified.summary[2],0,1800000001];
 const proofRaw=encode(fields),proof=[proofRaw,await directory.sign(join(text.encode('trailmate.geocache/receipt/v1\0'),proofRaw))],entry={publicKey:pub,delivery,epoch};
 assert.equal((await verifyPublicationReceipt(proof,signed,entry)).revisionHash,verified.revisionHash);
 await assert.rejects(verifyPublicationReceipt(proof,signed,{...entry,epoch:new Uint8Array(16)}));
 await assert.rejects(verifyPublicationReceipt(proof,signed,{...entry,publicKey:await (await Identity.generate()).getPublicKey()}));
 const bad=proofRaw.slice();bad[bad.length-1]^=1;await assert.rejects(verifyPublicationReceipt([bad,proof[1]],signed,entry));
 fields[4]=2;const other=encode(fields);await assert.rejects(verifyPublicationReceipt([other,await directory.sign(join(text.encode('trailmate.geocache/receipt/v1\0'),other))],signed,entry));
 const key='directory',remote={...entry,identity:directory,destination:delivery,ready:true,receiptSupported:true};let calls=0;
 const client={directories:new Map([[key,remote]]),enqueue:async fn=>fn(),request:async(target,op,body)=>{
  calls++;assert.equal(target,remote);assert.equal(op,5);assert.deepEqual(body,[verified.summary[0],verified.summary[2],await author.getPublicKey()]);return proof;
 }};
 assert.equal((await readDirectoryPublicationReceipt(client,key,signed,await author.getPublicKey())).revision,1);assert.equal(calls,1);
 remote.receiptSupported=false;await assert.rejects(readDirectoryPublicationReceipt(client,key,signed,await author.getPublicKey()),e=>e.code==='DIRECTORY_RECEIPT_UNSUPPORTED');assert.equal(calls,1);
 remote.receiptSupported=true;client.enqueue=async fn=>{client.directories.delete(key);return fn();};
 await assert.rejects(readDirectoryPublicationReceipt(client,key,signed,await author.getPublicKey()));assert.equal(calls,1);
 const values=new Map(),storage={getItem:key=>values.get(key)??null,setItem:(key,value)=>values.set(key,value)},locks={request:async(_name,_options,fn)=>fn({})};
 const account={verified:true,publicKey:await author.getPublicKey(),generation:7};
 const checkpoint=saveIPPublication({stage:'confirmed',authorKey:account.publicKey,authorGeneration:7,draftId:new Uint8Array(16).fill(1),draftGeneration:1n,
  operationId:new Uint8Array(16).fill(3),transferId:new Uint8Array(16).fill(4),hash:new Uint8Array(await crypto.subtle.digest('SHA-256',raw)),raw,signature:signed[1],
  directoryKey:Buffer.from(delivery).toString('hex')},{storage});
 let fetches=0;const network={ready:()=>true,publicationReceipt:async()=>{fetches++;return verifyPublicationReceipt(proof,signed,entry);}};
 const retained=await retainIPPublicationReceipt(checkpoint,account,network,{storage,locks});assert.deepEqual(retained.receipt.raw,proofRaw);assert.equal(fetches,1);
 const transferId=new Uint8Array(16).fill(7),authorName=(await Identity.fullHash(text.encode('lxmf.delivery'))).slice(0,10),authorDestination=await Identity.truncatedHash(join(authorName,author.identityHash));
 let usbCalls=0,total=0,offset=0;const chunks=[];
 const session={request:async(op,body)=>{usbCalls++;if(op===1)return[account.publicKey,author.identityHash,authorDestination,7];if(op===32){assert.equal(body[1],6);total=body[2];return[transferId,320,0,0];}if(op===33){assert.equal(body[1],offset);assert.ok(body[2].length<=320);chunks.push(...body[2]);offset+=body[2].length;return[transferId,320,offset,0];}if(op===35){assert.equal(offset,total);return[transferId,320,offset,1];}throw Error(op);}};
 await assert.rejects(uploadDevicePublicationConfirmation(session,signed,retained.receipt,account,{id:transferId,operations:[32,33,35]}));assert.equal(usbCalls,0);
 const upload=await uploadDevicePublicationConfirmation(session,signed,retained.receipt,account,{id:transferId,operations:[32,33,35,75,76]});assert.equal(upload.sealed,true);assert.deepEqual(Uint8Array.from(chunks),encode(signed));
 let admissions=0,statusReads=0,busyOnce=true;
 const pending=[transferId,upload.sha256,7,0,null,null,null,0];
 const complete=[transferId,upload.sha256,7,1,verified.summary[0],1,verified.summary[2],1];
 const confirmationSession={request:async(op,body)=>{
  if(op===1)return[account.publicKey,author.identityHash,authorDestination,7];
  if(op===75){admissions++;assert.deepEqual(body,[transferId,upload.sha256,7,proofRaw,proof[1],pub,epoch]);if(busyOnce){busyOnce=false;throw Object.assign(Error('Busy'),{code:5});}return pending;}
  if(op===76){statusReads++;assert.deepEqual(body,[transferId]);return complete;}
  throw Error(op);
 }};
 const admitted=await beginDevicePublicationConfirmation(confirmationSession,upload,account,{sleep:async()=>{}});
 assert.equal(admissions,2);assert.equal(admitted.confirmed,false);
 assert.equal((await readDevicePublicationConfirmation(confirmationSession,admitted.binding,account)).confirmed,true);assert.equal(statusReads,1);
 const confirmationRequest=confirmationSession.request;
 confirmationSession.request=async(op,body)=>{if(op===76)throw Object.assign(Error('Runtime guard busy'),{code:5});return confirmationRequest(op,body);};
 const busyStatus=await readDevicePublicationConfirmation(confirmationSession,admitted.binding,account);assert.equal(busyStatus.confirmed,false);assert.equal(busyStatus.busy,true);assert.equal(busyStatus.phase,undefined);assert.deepEqual(busyStatus.binding,admitted.binding);assert.equal(admissions,2);
 for(const code of [4,6,9,10,'TIMEOUT']){confirmationSession.request=async(op,body)=>{if(op===76)throw Object.assign(Error('Rejected'),{code});return confirmationRequest(op,body);};await assert.rejects(readDevicePublicationConfirmation(confirmationSession,admitted.binding,account),error=>error.code===code);}
 const confirmationAbort=new AbortController();confirmationSession.request=async(op,body)=>{if(op===76){confirmationAbort.abort(new DOMException('Stopped','AbortError'));throw Object.assign(Error('Busy'),{code:5});}return confirmationRequest(op,body);};await assert.rejects(readDevicePublicationConfirmation(confirmationSession,admitted.binding,account,{signal:confirmationAbort.signal}),error=>error.name==='AbortError');
 confirmationSession.request=confirmationRequest;
 complete[7]=0;await assert.rejects(readDevicePublicationConfirmation(confirmationSession,admitted.binding,account),/durable/);complete[7]=1;
 complete[6]=new Uint8Array(32);await assert.rejects(readDevicePublicationConfirmation(confirmationSession,admitted.binding,account),/durable/);complete[6]=verified.summary[2];
 let lostCalls=0;const lostSession={request:async(op)=>{if(op===1)return[account.publicKey,author.identityHash,authorDestination,7];lostCalls++;throw Error('Reply lost');}};
 await assert.rejects(beginDevicePublicationConfirmation(lostSession,upload,account,{sleep:async()=>{}}),error=>error.code==='CONFIRMATION_UNCONFIRMED'&&error.binding.revision===1);assert.equal(lostCalls,1);
 await assert.rejects(readDevicePublicationConfirmation(confirmationSession,admitted.binding,{...account,generation:8}),/binding/);
 let closes=0,leasedStatusReads=0;offset=0;chunks.length=0;
 const leasedSession={request:async(op,body)=>{
  assert.equal(closes,0,'maintenance lease must span upload and confirmation');
  if(op===3){closes++;return [];}
  if(op===75)return pending;
  if(op===76){leasedStatusReads++;if(leasedStatusReads===1)throw Object.assign(Error('Runtime guard busy'),{code:5});return complete;}
  return session.request(op,body);
 }};
 const reader=new DeviceDraftReader(leasedSession);
 const synced=await reader.confirmPublication(signed,retained.receipt,account,{transferId,operations:[32,33,35,75,76]});
 assert.equal(synced.confirmed,true);assert.equal(closes,1);assert.equal(leasedStatusReads,2);await reader.close();
 let syncAttempts=0;const synchronizedIds=[];
 const syncReader={confirmPublication:async(content,receipt,identity,settings)=>{
  syncAttempts++;synchronizedIds.push(settings.transferId.slice());assert.deepEqual(content,signed);assert.deepEqual(receipt.raw,proofRaw);assert.equal(identity.generation,7);
  assert.equal(loadIPPublication(account.publicKey,{storage}).deviceSync.stage,'pending','checkpoint precedes USB side effects');
  if(syncAttempts===1)throw Error('USB response lost');return {confirmed:true};
 }};
 const syncOptions={storage,locks,operations:[32,33,35,75,76],randomBytes:size=>new Uint8Array(size).fill(8)};
 await assert.rejects(syncIPPublicationToDevice(syncReader,retained,account,syncOptions),/lost/);
 const pendingSync=loadIPPublication(account.publicKey,{storage});assert.equal(pendingSync.deviceSync.stage,'pending');
 const completeSync=await syncIPPublicationToDevice(syncReader,pendingSync,account,syncOptions);assert.equal(completeSync.deviceSync.stage,'confirmed');
 assert.deepEqual(synchronizedIds[0],synchronizedIds[1]);
 await syncIPPublicationToDevice(syncReader,completeSync,account,syncOptions);assert.equal(syncAttempts,2);
 let recovered=loadIPPublication(account.publicKey,{storage});await retainIPPublicationReceipt(recovered,account,null,{storage,locks});assert.equal(fetches,1);
 // Pre-generation checkpoints retain their canonical v1 format when loaded.
 const oldSync={...recovered.deviceSync};delete oldSync.generation;
 recovered=saveIPPublication({...recovered,deviceSync:oldSync},{storage,expected:recovered});
 assert.equal(recovered.deviceSync.generation,undefined);
 const newAccount={...account,generation:8},newIds=[];let newAttempts=0,newRandom=0;
 const newReader={confirmPublication:async(content,receipt,current,settings)=>{
  newAttempts++;newIds.push(settings.transferId.slice());assert.equal(current.generation,8);
  assert.deepEqual(content,signed);assert.deepEqual(receipt.raw,proofRaw);
  const saved=loadIPPublication(account.publicKey,{storage});assert.equal(saved.deviceSync.stage,'pending');assert.equal(saved.deviceSync.generation,8);
  if(newAttempts===1)throw Error('new generation reply lost');return {confirmed:true};
 }};
 const newOptions={...syncOptions,randomBytes:size=>{newRandom++;return new Uint8Array(size).fill(9);}};
 await assert.rejects(syncIPPublicationToDevice(newReader,recovered,newAccount,newOptions),/reply lost/);
 const newPending=loadIPPublication(account.publicKey,{storage});assert.equal(newPending.deviceSync.generation,8);
 assert.notDeepEqual(newPending.deviceSync.id,recovered.deviceSync.id);
 const newComplete=await syncIPPublicationToDevice(newReader,newPending,newAccount,newOptions);
 assert.deepEqual(newIds[0],newIds[1]);assert.equal(newRandom,1);assert.equal(newComplete.deviceSync.generation,8);
 assert.equal(newComplete.authorGeneration,7);assert.deepEqual(newComplete.raw,recovered.raw);assert.deepEqual(newComplete.signature,recovered.signature);
 await syncIPPublicationToDevice(newReader,newComplete,newAccount,newOptions);assert.equal(newAttempts,2);
 assert.throws(()=>saveIPPublication({...newComplete,deviceSync:{...newComplete.deviceSync,generation:0}},{storage,expected:newComplete}));
 recovered=newComplete;
 const restored=await retainIPPublicationReceipt(recovered,{...account,generation:8},null,{storage,locks});
 assert.equal(restored.authorGeneration,7);assert.deepEqual(restored.raw,recovered.raw);assert.deepEqual(restored.receipt,recovered.receipt);assert.equal(fetches,1);
 await assert.rejects(retainIPPublicationReceipt(recovered,{...account,generation:0},network,{storage,locks}),/identity changed/);
 await assert.rejects(retainIPPublicationReceipt(recovered,{...account,publicKey:await (await Identity.generate()).getPublicKey()},network,{storage,locks}),/identity changed/);
 const damaged={...recovered,receipt:{...recovered.receipt,signature:new Uint8Array(64)}};saveIPPublication(damaged,{storage,expected:recovered});
 await assert.rejects(retainIPPublicationReceipt(damaged,account,network,{storage,locks}));assert.equal(fetches,1);
});
