import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {encodeIdentityBackupPayload,decodeIdentityBackupPayload,identityBackupSigningBytes} from '../../site/device/src/identity-backup-payload.js';
import {encryptIdentityBackup} from '../../site/device/src/identity-backup-file.js';
import {previewEncryptedDeviceIdentity} from '../../site/device/src/identity-backup-preview.js';
test('seed restore requires explicit formats and rechecks support before its single write',async()=>{
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),privateKey=new Uint8Array(64),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),delivery=await Identity.truncatedHash(Uint8Array.from([...prefix,...identity.identityHash]));
 const exported=await identity.getPrivateKey();privateKey.set(exported.subarray(0,32));privateKey.set(exported.subarray(64,96),32);exported.fill(0);
 const payload=encodeIdentityBackupPayload({format:1,publicKey,privateKey,identityHash:identity.identityHash,delivery,generation:3}),password='seed identity restore password';
 const encrypted=await encryptIdentityBackup([payload,await identity.sign(identityBackupSigningBytes(payload))],{verified:true,publicKey,generation:3},password);
 let formats=[0,1];const calls=[];
 const session={id:new Uint8Array(16).fill(2),async request(op,body){calls.push(op);const info=[publicKey,identity.identityHash,delivery,7];if(op===1)return info;if(op===2)return [...info,await identity.sign(authorProofMessage(this.id,body[0],publicKey,7))];if(op===6){assert.deepEqual(body,[]);return formats;}assert.equal(op,5);assert.equal(body[0],7);assert.deepEqual(body[1],publicKey);assert.equal(body[4],true);const decoded=decodeIdentityBackupPayload(body[2]);assert.equal(decoded.format,1);assert.deepEqual(decoded.privateKey,privateKey);decoded.privateKey.fill(0);return [0,8];}};
 const legacy=await previewEncryptedDeviceIdentity(encrypted,password,{session,operations:[1,2,5]});assert.equal(legacy.metadata.restoreSupported,false);await assert.rejects(legacy.restore({consent:true}),{code:'IDENTITY_RESTORE_FORMAT_UNSUPPORTED'});legacy.close();assert.equal(calls.includes(5),false);assert.equal(calls.includes(6),false);
 const supported=await previewEncryptedDeviceIdentity(encrypted,password,{session,operations:[1,2,5,6]});assert.equal(supported.metadata.restoreSupported,true);const result=await supported.restore({consent:true});assert.equal(result.state,'reboot-required');assert.equal(supported.closed,true);assert.equal(calls.filter(op=>op===5).length,1);assert.equal(calls.filter(op=>op===6).length,2);
 const changed=await previewEncryptedDeviceIdentity(encrypted,password,{session,operations:[1,2,5,6]});formats=[0];await assert.rejects(changed.restore({consent:true}),{code:'IDENTITY_RESTORE_FORMAT_UNSUPPORTED'});assert.equal(calls.filter(op=>op===5).length,1);assert.equal(changed.closed,true);
 privateKey.fill(0);payload.fill(0);
});
