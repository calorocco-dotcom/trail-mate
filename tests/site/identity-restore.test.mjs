import test from 'node:test';import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {encodeIdentityBackupPayload,identityBackupSigningBytes} from '../../site/device/src/identity-backup-payload.js';
import {encryptIdentityBackup} from '../../site/device/src/identity-backup-file.js';
import {previewEncryptedDeviceIdentity} from '../../site/device/src/identity-backup-preview.js';
import {verifyRestoredDeviceIdentity} from '../../site/device/src/identity-restore-verification.js';
const password='restore identity passphrase';
async function publicIdentity(){const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),delivery=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash]));return {identity,publicKey,delivery};}
async function fixture(){
 const backup=await publicIdentity(),target=await publicIdentity(),calls=[];let current=target,generation=7,mode=0,received;
 const payload=encodeIdentityBackupPayload({format:0,publicKey:backup.publicKey,privateKey:new Uint8Array(96).fill(7),identityHash:backup.identity.identityHash,delivery:backup.delivery,generation:3});
 const encrypted=await encryptIdentityBackup([payload,await backup.identity.sign(identityBackupSigningBytes(payload))],{verified:true,publicKey:backup.publicKey,generation:3},password);
 const session={id:new Uint8Array(16).fill(2),async request(op,body){calls.push(op);const info=[current.publicKey,current.identity.identityHash,current.delivery,generation];if(op===1)return info;if(op===2)return [...info,await current.identity.sign(authorProofMessage(this.id,body[0],current.publicKey,generation))];assert.equal(op,5);assert.equal(body[0],7);assert.deepEqual(body[1],target.publicKey);assert.equal(body[4],true);received=body;
  if(mode==='busy')throw Object.assign(Error('busy'),{code:5});current=backup;generation=8;
  if(mode==='timeout')throw Object.assign(Error('timeout'),{code:'TIMEOUT'});if(typeof mode==='function')mode();return [typeof mode==='number'?mode:0,8];}};
 return {encrypted,session,calls,get received(){return received;},set mode(value){mode=value;},advance(){generation++;}};
}
test('restore switches author only after fresh proof, then requires a reconnect proof',async()=>{
 const f=await fixture(),preview=await previewEncryptedDeviceIdentity(f.encrypted,password,{session:f.session});assert.equal(preview.metadata.changesIdentity,true);
 const count=f.calls.length;await assert.rejects(preview.restore(),error=>error.code==='IDENTITY_BACKUP_CONSENT_REQUIRED');assert.equal(f.calls.length,count);
 const result=await preview.restore({consent:true});assert.equal(result.state,'reboot-required');assert.equal(preview.closed,true);assert.deepEqual(f.calls,[1,2,1,2,5]);
 assert.ok(f.received[2].every(byte=>byte===0)&&f.received[3].every(byte=>byte===0));
 f.session.id=new Uint8Array(16).fill(4);assert.equal((await verifyRestoredDeviceIdentity(f.session,result.expectation)).state,'verified');
 f.advance();await assert.rejects(verifyRestoredDeviceIdentity(f.session,result.expectation),error=>error.code==='IDENTITY_RESTORE_NOT_CONFIRMED');
 await assert.rejects(preview.restore({consent:true}),error=>error.code==='IDENTITY_BACKUP_PREVIEW_CLOSED');
});
test('uncertain commit and lost response retain only public expectations',async()=>{
 for(const mode of [1,'timeout']){const f=await fixture();f.mode=mode;const preview=await previewEncryptedDeviceIdentity(f.encrypted,password,{session:f.session});let expectation;
  if(mode===1){const result=await preview.restore({consent:true});assert.equal(result.state,'indeterminate');expectation=result.expectation;}
  else{await assert.rejects(preview.restore({consent:true}),error=>{assert.equal(error.code,'IDENTITY_RESTORE_OUTCOME_UNKNOWN');expectation=error.expectation;return true;});}
  assert.deepEqual(Object.keys(expectation).sort(),['delivery','generation','identityHash','publicKey']);assert.equal(preview.closed,true);assert.equal((await verifyRestoredDeviceIdentity(f.session,expectation)).state,'verified');
 }
});
test('stale targets and explicit rejection do not trigger automatic retries',async()=>{
 const stale=await fixture(),preview=await previewEncryptedDeviceIdentity(stale.encrypted,password,{session:stale.session});stale.advance();await assert.rejects(preview.restore({consent:true}),error=>error.code==='IDENTITY_BACKUP_TARGET_CHANGED');assert.ok(!stale.calls.includes(5));
 const busy=await fixture();busy.mode='busy';const blocked=await previewEncryptedDeviceIdentity(busy.encrypted,password,{session:busy.session});await assert.rejects(blocked.restore({consent:true}),error=>error.code===5);assert.equal(blocked.closed,true);assert.equal(busy.calls.filter(op=>op===5).length,1);
});
test('cancellation after sending is an unknown outcome and clears private copies',async()=>{
 const f=await fixture(),controller=new AbortController();f.mode=()=>controller.abort();const preview=await previewEncryptedDeviceIdentity(f.encrypted,password,{session:f.session,signal:controller.signal});
 await assert.rejects(preview.restore({consent:true}),error=>error.code==='IDENTITY_RESTORE_OUTCOME_UNKNOWN');assert.equal(preview.closed,true);assert.ok(f.received[2].every(byte=>byte===0));
});
