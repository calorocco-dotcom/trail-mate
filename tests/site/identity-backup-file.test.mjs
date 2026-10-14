import test from 'node:test';import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeIdentityBackupPayload,identityBackupSigningBytes} from '../../site/device/src/identity-backup-payload.js';
import {encryptIdentityBackup,decryptIdentityBackup,isEncryptedIdentityBackup,clearDecryptedIdentityBackup} from '../../site/device/src/identity-backup-file.js';
const password='identity backup passphrase';
async function fixture(format=0){const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),delivery=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash])),payload=encodeIdentityBackupPayload({format,publicKey,privateKey:new Uint8Array(format?64:96).fill(7),identityHash:identity.identityHash,delivery,generation:3});return {signed:[payload,await identity.sign(identityBackupSigningBytes(payload))],author:{verified:true,publicKey,generation:3}};}
test('identity files encrypt both representations and provide explicit plaintext cleanup',async()=>{
 for(const format of [0,1]){const f=await fixture(format),encrypted=await encryptIdentityBackup(f.signed,f.author,password);assert.equal(isEncryptedIdentityBackup(encrypted),true);assert.ok(encrypted.length<=504);assert.notDeepEqual(encrypted.slice(40,136),new Uint8Array(96).fill(7));const decoded=await decryptIdentityBackup(encrypted,password);assert.equal(decoded.identity.format,format);assert.equal(decoded.identity.privateKey.length,format?64:96);assert.equal(decoded.identity.privateKey[0],7);clearDecryptedIdentityBackup(decoded);assert.ok(decoded.identity.privateKey.every(value=>value===0));assert.ok(decoded.signed[0].every(value=>value===0));}
});
test('wrong password, salt or ciphertext and hostile derivation cost are rejected',async()=>{
 const f=await fixture(),encrypted=await encryptIdentityBackup(f.signed,f.author,password);await assert.rejects(decryptIdentityBackup(encrypted,'wrong password'),error=>error.code==='IDENTITY_BACKUP_AUTH_FAILED');
 for(const index of [12,28,encrypted.length-1]){const changed=encrypted.slice();changed[index]^=1;await assert.rejects(decryptIdentityBackup(changed,password),error=>error.code==='IDENTITY_BACKUP_AUTH_FAILED');}
 const cost=encrypted.slice();new DataView(cost.buffer).setUint32(8,0xffffffff,false);await assert.rejects(decryptIdentityBackup(cost,password),error=>error.code==='IDENTITY_BACKUP_PARAMETERS_UNSUPPORTED');
 const wrongFormat=encrypted.slice();wrongFormat[2]^=1;await assert.rejects(decryptIdentityBackup(wrongFormat,password),error=>error.code==='IDENTITY_BACKUP_FORMAT_INVALID');
});
