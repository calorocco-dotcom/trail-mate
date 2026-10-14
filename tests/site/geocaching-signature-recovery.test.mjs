import test from 'node:test';
import assert from 'node:assert/strict';
import {readSignatureRecovery,recoverSignatureOwner,recoverConfirmationOwner} from '../../site/geocaching/src/device-signature-recovery.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {CAPABILITY} from '../../site/device/src/hostlink.js';
const author={verified:true,publicKey:new Uint8Array(64).fill(2),generation:7};
const id=new Uint8Array(16).fill(3),owner=new Uint8Array(16).fill(4),draftId=new Uint8Array(16).fill(5),raw=new Uint8Array(100).fill(6);
const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',raw));
const options={capabilities:CAPABILITY.SHARED_SIGNATURE_RECOVERY,operations:[38],retryMs:1};
const row=[id,5,raw.length,hash,7,1,raw.length,owner,draftId];
test('Legacy firmware is never queried; supported recovery is read-only and binds exact signing input',async()=>{
 let queries=0;const session={async request(op,body){assert.equal(op,38);assert.deepEqual(body,[5,null]);queries++;return [1,null,row];}};
 assert.equal(await readSignatureRecovery(session,author,{}),null);assert.equal(queries,0);
 const record={stage:'signing',authorKey:author.publicKey,authorGeneration:7,transferId:id,draftId,raw,hash};
 const recovered=await recoverSignatureOwner(session,record,author,options);assert.deepEqual(recovered,owner);assert.notEqual(recovered,owner);assert.equal(queries,1);
 for(const changed of [{transferId:new Uint8Array(16)},{hash:new Uint8Array(32)},{draftId:new Uint8Array(16)},{raw:new Uint8Array(100)}])
  await assert.rejects(recoverSignatureOwner(session,{...record,...changed},author,options),{code:'GEO_SAVE_OWNER_CONFLICT'});
 assert.equal(await recoverSignatureOwner(session,{...record,authorGeneration:6},author,options),null);
 assert.equal(await recoverSignatureOwner(session,{...record,stage:'signed'},author,options),null);
});
test('Confirmation recovery requires its own capability and exact original signed input',async()=>{
 const signature=new Uint8Array(64).fill(7),source=encode([raw,signature]),signedHash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
 const record={stage:'confirmed',authorKey:author.publicKey,authorGeneration:7,transferId:id,draftId,raw,hash,signature,deviceSync:{stage:'pending',id,generation:7}};
 let calls=0;const session={async request(op,body){calls++;assert.equal(op,38);assert.deepEqual(body,[6,null]);return [1,null,[id,6,source.length,signedHash,7,1,source.length,owner,draftId]];}};
 assert.equal(await recoverConfirmationOwner(session,record,author,options),null);assert.equal(calls,0);
 const supported={...options,capabilities:CAPABILITY.SHARED_CONFIRMATION_RECOVERY};
 assert.deepEqual(await recoverConfirmationOwner(session,record,author,supported),owner);
 await assert.rejects(recoverConfirmationOwner(session,{...record,signature:new Uint8Array(64)},author,supported),{code:'GEO_SAVE_OWNER_CONFLICT'});
 assert.deepEqual(await recoverConfirmationOwner(session,{...record,deviceSync:{...record.deviceSync,stage:'confirmed'}},author,supported),owner);
 assert.equal(await recoverConfirmationOwner(session,{...record,deviceSync:{...record.deviceSync,generation:6}},author,supported),null);
});
test('Busy waits on the same metadata query; corruption, identity mismatch and lost reply cannot authorize cleanup',async()=>{
 let queries=0;const session={async request(){if(!queries++)throw Object.assign(Error('busy'),{code:5});return [1,null,row];}};
 assert.deepEqual((await readSignatureRecovery(session,author,options)).id,id);assert.equal(queries,2);
 assert.equal(await readSignatureRecovery({request:async()=>[1,null,null]},author,options),null);
 for(const result of [[1,4,row],[1,null,[...row.slice(0,4),6,...row.slice(5)]],[1,null,[...row.slice(0,5),0,...row.slice(6)]],[1,null,[...row.slice(0,7),new Uint8Array(16),draftId]]])
  await assert.rejects(readSignatureRecovery({request:async()=>result},author,options),{code:'GEO_SAVE_OWNER_CONFLICT'});
 let count=0;await assert.rejects(readSignatureRecovery({async request(){count++;throw Object.assign(Error('Lost reply'),{code:'TIMEOUT'});}},author,options),{code:'TIMEOUT'});assert.equal(count,1);
 const controller=new AbortController();controller.abort(Error('stop'));
 await assert.rejects(readSignatureRecovery(session,author,{...options,signal:controller.signal}),/stop/);assert.equal(queries,2);
});
test('Recovery freezes author and checkpoint before a reply and respects cancellation during that reply',async()=>{
 const mutableAuthor={...author,publicKey:author.publicKey.slice()},record={stage:'signing',authorKey:author.publicKey.slice(),authorGeneration:7,transferId:id.slice(),draftId:draftId.slice(),raw:raw.slice(),hash:hash.slice()};
 const session={async request(){mutableAuthor.generation=8;mutableAuthor.publicKey.fill(9);record.transferId.fill(0);record.hash.fill(0);record.raw.fill(0);return [1,null,row];}};
 assert.deepEqual(await recoverSignatureOwner(session,record,mutableAuthor,options),owner);
 const controller=new AbortController();
 await assert.rejects(readSignatureRecovery({async request(){controller.abort(Error('cancelled during reply'));return [1,null,row];}},author,{...options,signal:controller.signal}),/cancelled during reply/);
});
