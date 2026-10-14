import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {decode} from '../../site/geocaching/src/protocol.js';
import {decodeManagement,encodeManagement,OPERATION} from '../../site/device/src/management-protocol.js';
import {verifyRadioIdentityBackupPayload,encryptRadioIdentityBackup,decryptRadioIdentityBackup,clearRadioIdentityBackup} from '../../site/device/src/radio-identity.js';

const executable=path.resolve('.codex-build/hostlink-codec/native/radio_identity_management'+(process.platform==='win32'?'.exe':''));
test('actual C++ RF export -> browser verification/encryption -> actual C++ restore, both native key types',async()=>{
 const frames=execFileSync(executable,['--export'],{encoding:'utf8'}).trim().split(/\r?\n/);
 assert.equal(frames.length,2);
 const requests=[],keys=[];
 for(let index=0;index<frames.length;index++){
  const response=decodeManagement(Uint8Array.from(Buffer.from(frames[index],'hex')));
  assert.equal(response.code,0);
  const identity=await verifyRadioIdentityBackupPayload(response.body);
  const author={verified:true,publicKey:identity.sourceAuthor,generation:7,exportSession:response.session,exportRequest:response.request};
  const verified=await verifyRadioIdentityBackupPayload(response.body,author);
  assert.equal(verified.kind,index+1);assert.equal(verified.radioGeneration,1);assert.equal(verified.privateKey.length,index===0?64:32);
  const encrypted=await encryptRadioIdentityBackup(response.body,author,'native RF interoperability password');
  assert.ok(encrypted.length<=504);
  const restored=await decryptRadioIdentityBackup(encrypted,'native RF interoperability password');
  assert.deepEqual(restored.identity.privateKey,verified.privateKey);
  keys.push(Buffer.from(verified.publicKey).toString('hex'));
  const request=encodeManagement(response.session,response.request,OPERATION.RADIO_IDENTITY_BACKUP_RESTORE,
   [7,author.publicKey,restored.signed[0],restored.signed[1],1,verified.publicKey,true]);
  assert.ok(request.length<=512);requests.push(Buffer.from(request).toString('hex'));request.fill(0);
  identity.privateKey.fill(0);verified.privateKey.fill(0);clearRadioIdentityBackup(restored);response.body.forEach(value=>value.fill(0));
 }
 const results=execFileSync(executable,['--restore'],{encoding:'utf8',input:requests.join('\n')+'\n'}).trim().split(/\r?\n/);
 assert.equal(results.length,2);
 for(let index=0;index<results.length;index++){
  const response=decode(Uint8Array.from(Buffer.from(results[index],'hex')),448);
  assert.deepEqual(response.slice(0,3),[0,index+1,2]);assert.equal(Buffer.from(response[3]).toString('hex'),keys[index]);
 }
});
