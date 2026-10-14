import test from 'node:test';import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {encodeIdentityBackupPayload,identityBackupSigningBytes} from '../../site/device/src/identity-backup-payload.js';
import {exportEncryptedDeviceIdentity} from '../../site/device/src/identity-backup-export.js';
import {decryptIdentityBackup,clearDecryptedIdentityBackup} from '../../site/device/src/identity-backup-file.js';
async function fixture(){const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),delivery=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash])),payload=encodeIdentityBackupPayload({format:0,publicKey,privateKey:new Uint8Array(96).fill(7),identityHash:identity.identityHash,delivery,generation:3}),signed=[payload,await identity.sign(identityBackupSigningBytes(payload))],calls=[];const session={id:new Uint8Array(16).fill(2),async request(op,body){calls.push(op);const info=[publicKey,identity.identityHash,delivery,3];if(op===1)return info;if(op===2)return [...info,await identity.sign(authorProofMessage(this.id,body[0],publicKey,3))];assert.equal(op,4);assert.deepEqual(body,[3,true]);return signed;}};return {session,signed,calls};}
test('USB export proves current author and returns only encrypted bytes',async()=>{
 const f=await fixture(),result=await exportEncryptedDeviceIdentity(f.session,'identity backup password',{consent:true});assert.deepEqual(f.calls,[1,2,4]);assert.equal(result.author.verified,true);assert.equal(Object.hasOwn(result,'privateKey'),false);assert.ok(f.signed[0].every(value=>value===0));const restored=await decryptIdentityBackup(result.bytes,'identity backup password');assert.equal(restored.identity.privateKey[0],7);clearDecryptedIdentityBackup(restored);
});
test('consent, password and crypto availability are checked before USB traffic',async()=>{
 const session={request(){assert.fail('unexpected USB');}};
 await assert.rejects(exportEncryptedDeviceIdentity(session,'identity backup password'),error=>error.code==='IDENTITY_BACKUP_CONSENT_REQUIRED');await assert.rejects(exportEncryptedDeviceIdentity(session,'short',{consent:true}),error=>error.code==='IDENTITY_BACKUP_PASSWORD_INVALID');await assert.rejects(exportEncryptedDeviceIdentity(session,'identity backup password',{consent:true,cryptoProvider:null}),error=>error.code==='IDENTITY_BACKUP_CRYPTO_UNAVAILABLE');
});
test('a late private response after cancellation is cleared and never returned as a file',async()=>{
 const f=await fixture(),controller=new AbortController(),request=f.session.request;
 f.session.request=async function(operation,...args){const reply=await request.call(this,operation,...args);if(operation===4)controller.abort();return reply;};
 await assert.rejects(exportEncryptedDeviceIdentity(f.session,'identity backup password',{consent:true,signal:controller.signal}),error=>error.name==='AbortError');
 assert.deepEqual(f.calls,[1,2,4]);assert.ok(f.signed[0].every(value=>value===0));assert.ok(f.signed[1].every(value=>value===0));
});
