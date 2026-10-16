import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeIdentityBackupPayload,identityBackupSigningBytes} from '../../site/device/src/identity-backup-payload.js';
import {encryptIdentityBackup} from '../../site/device/src/identity-backup-file.js';
import {previewEncryptedDeviceIdentity} from '../../site/device/src/identity-backup-preview.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
const password='identity preview passphrase';
async function fixture(){
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),delivery=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash]));
 const payload=encodeIdentityBackupPayload({format:0,publicKey,privateKey:new Uint8Array(96).fill(7),identityHash:identity.identityHash,delivery,generation:3});
 const encrypted=await encryptIdentityBackup([payload,await identity.sign(identityBackupSigningBytes(payload))],{verified:true,publicKey,generation:3},password);
 return {encrypted,identity,publicKey,delivery,hash:Array.from(identity.identityHash,byte=>byte.toString(16).padStart(2,'0')).join('')};
}
test('offline preview exposes only public metadata and closes irreversibly',async()=>{
 const {encrypted,hash}=await fixture(),preview=await previewEncryptedDeviceIdentity(encrypted,password);
 assert.equal(preview.metadata.identityHash,hash);assert.equal(preview.metadata.backupGeneration,3);assert.equal(preview.metadata.changesIdentity,null);
 assert.equal('privateKey' in preview.metadata,false);assert.equal('signed' in preview,false);assert.ok(Object.isFrozen(preview.metadata));
 await assert.rejects(preview.revalidateTarget(),error=>error.code==='IDENTITY_BACKUP_TARGET_REQUIRED');
 preview.close();preview.close();assert.equal(preview.closed,true);
 await assert.rejects(preview.revalidateTarget(),error=>error.code==='IDENTITY_BACKUP_PREVIEW_CLOSED');
});
test('abort after preview disposal prevents future use; pre-abort avoids USB',async()=>{
 const {encrypted}=await fixture(),controller=new AbortController(),preview=await previewEncryptedDeviceIdentity(encrypted,password,{signal:controller.signal});
 controller.abort();assert.equal(preview.closed,true);await assert.rejects(preview.revalidateTarget(),error=>error.name==='AbortError');
 let calls=0;await assert.rejects(previewEncryptedDeviceIdentity(encrypted,password,{signal:controller.signal,session:{request(){calls++;}}}),error=>error.name==='AbortError');assert.equal(calls,0);
});
test('invalid password never requests the device identity',async()=>{
 const {encrypted}=await fixture();let calls=0;
 await assert.rejects(previewEncryptedDeviceIdentity(encrypted,'incorrect',{session:{request(){calls++;}}}),error=>error.code==='IDENTITY_BACKUP_AUTH_FAILED');assert.equal(calls,0);
});
test('live proof distinguishes identity changes and rejects a changed target generation',async()=>{
 const f=await fixture();let generation=9;
 const session={id:new Uint8Array(16).fill(2),async request(op,body){
  const info=[f.publicKey,f.identity.identityHash,f.delivery,generation];
  if(op===1)return info;assert.equal(op,2);
  return [...info,await f.identity.sign(authorProofMessage(this.id,body[0],f.publicKey,generation))];
 }};
 const preview=await previewEncryptedDeviceIdentity(f.encrypted,password,{session});
 assert.equal(preview.metadata.changesIdentity,false);assert.equal(preview.metadata.targetGeneration,9);
 assert.equal(await preview.revalidateTarget(),preview.metadata);
 generation++;await assert.rejects(preview.revalidateTarget(),error=>error.code==='IDENTITY_BACKUP_TARGET_CHANGED');preview.close();
 const other=await fixture();
 const different=await previewEncryptedDeviceIdentity(other.encrypted,password,{session});
 assert.equal(different.metadata.changesIdentity,true);different.close();
});
