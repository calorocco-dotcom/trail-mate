import test from 'node:test';import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeIdentityBackupPayload,decodeIdentityBackupPayload,identityBackupSigningBytes,verifyIdentityBackupPayload} from '../../site/device/src/identity-backup-payload.js';
async function fixture(format=0){const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),delivery=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash])),privateKey=new Uint8Array(format===0?96:64).fill(7),payload=encodeIdentityBackupPayload({format,publicKey,privateKey,identityHash:identity.identityHash,delivery,generation:3});return {identity,payload,author:{verified:true,publicKey,generation:3},privateKey};}
test('expanded and seed representations retain their exact widths and authenticated identity',async()=>{
 for(const format of [0,1]){const f=await fixture(format),signed=[f.payload,await f.identity.sign(identityBackupSigningBytes(f.payload))],result=await verifyIdentityBackupPayload(signed,f.author);assert.equal(result.format,format);assert.deepEqual(result.privateKey,f.privateKey);assert.ok(f.payload.length<=320);f.payload.fill(0);assert.equal(result.privateKey[0],7);}
});
test('wrong representation width, address, identity generation and signature are rejected',async()=>{
 const f=await fixture(),decoded=decodeIdentityBackupPayload(f.payload);
 assert.throws(()=>encodeIdentityBackupPayload({...decoded,privateKey:new Uint8Array(64).fill(7)}));
 const signature=await f.identity.sign(identityBackupSigningBytes(f.payload));await assert.rejects(verifyIdentityBackupPayload([f.payload,signature],{...f.author,generation:4}),error=>error.code==='IDENTITY_BACKUP_AUTHOR_MISMATCH');
 signature[0]^=1;await assert.rejects(verifyIdentityBackupPayload([f.payload,signature],f.author),error=>error.code==='IDENTITY_BACKUP_SIGNATURE_INVALID');
 const wrong=encodeIdentityBackupPayload({...decoded,delivery:new Uint8Array(16).fill(1)});await assert.rejects(verifyIdentityBackupPayload([wrong,await f.identity.sign(identityBackupSigningBytes(wrong))],f.author),error=>error.code==='IDENTITY_BACKUP_ADDRESS_MISMATCH');
});
