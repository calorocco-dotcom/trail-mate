import test from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {encryptConfigBackup,decryptConfigBackup,decryptConfigBackupDetails,isEncryptedConfigBackup,ENCRYPTED_BACKUP_MAX_BYTES} from '../../site/device/src/config-backup.js';
const utf8=new TextEncoder(),password='a long backup password';
const document=new TmsDocument(utf8.encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Mountain\nmt.primary_psk=b64:c2VjcmV0\nEND\n'));
const options={cryptoProvider:webcrypto};
test('backup failures expose stable localization codes',async()=>{
 await assert.rejects(encryptConfigBackup(document,'short',options),{code:'BACKUP_PASSWORD_INVALID'});
 await assert.rejects(encryptConfigBackup(document,password,{cryptoProvider:{}}),{code:'BACKUP_CRYPTO_UNAVAILABLE'});
 await assert.rejects(decryptConfigBackup(new Uint8Array(),password,options),{code:'BACKUP_FORMAT_INVALID'});
 const bytes=await encryptConfigBackup(document,password,options);
 await assert.rejects(decryptConfigBackup(bytes,'wrong password',options),{code:'BACKUP_AUTH_FAILED'});
 const unsupported=bytes.slice();new DataView(unsupported.buffer).setUint32(8,1,false);
 await assert.rejects(decryptConfigBackup(unsupported,password,options),{code:'BACKUP_PARAMETERS_UNSUPPORTED'});
});
test('real Web Crypto encrypts a complete backup and authenticates its header and payload',async()=>{
 const encrypted=await encryptConfigBackup(document,password,options);
 assert.equal(isEncryptedConfigBackup(encrypted),true);assert.ok(encrypted.length<=ENCRYPTED_BACKUP_MAX_BYTES);
 assert.equal(new TextDecoder().decode(encrypted).includes('Mountain'),false);
 const restored=await decryptConfigBackup(encrypted,password,options);
 assert.equal(restored.kind,'backup');assert.deepEqual(restored.serialize(),document.serialize({kind:'backup'}));
 assert.equal(document.kind,'working');assert.equal(document.get('device.node_name').value,'Mountain');
 for(const offset of [12,28,40,encrypted.length-1]){
  const changed=encrypted.slice();changed[offset]^=1;
  await assert.rejects(decryptConfigBackup(changed,password,options),/口令不正确或文件已损坏/);
 }
 await assert.rejects(decryptConfigBackup(encrypted,'incorrect password',options),/口令不正确或文件已损坏/);
});
test('fresh random salt and nonce produce different backups and buffers freeze before async work',async()=>{
 const first=await encryptConfigBackup(document,password,options),second=await encryptConfigBackup(document,password,options);
 assert.notDeepEqual(first.slice(12,40),second.slice(12,40));assert.notDeepEqual(first,second);
 const work=decryptConfigBackup(first,password,options);first.fill(0);
 assert.equal((await work).get('device.node_name').value,'Mountain');
});
test('unsupported formats and KDF costs fail before calling cryptography',async()=>{
 const valid=await encryptConfigBackup(document,password,options);let calls=0;
 const forbidden={cryptoProvider:{getRandomValues(){},subtle:{importKey(){calls++;throw Error('unexpected');}}}};
 const altered=valid.slice();new DataView(altered.buffer).setUint32(8,0xffffffff,false);
 for(const invalid of [new Uint8Array(),new Uint8Array(ENCRYPTED_BACKUP_MAX_BYTES+1),altered,valid.slice(0,40)])
  await assert.rejects(decryptConfigBackup(invalid,password,forbidden));
 assert.equal(calls,0);
});
test('weak export passwords, malformed Unicode and unavailable crypto cannot produce a backup',async()=>{
 for(const invalid of ['', 'short','a'.repeat(1025),'long-password-\ud800'])await assert.rejects(encryptConfigBackup(document,invalid,options));
 await assert.rejects(encryptConfigBackup(document,password,{cryptoProvider:{}}),/无法使用/);
});
test('encrypted provenance is bounded, whitelisted and preserved without replacing the document',async()=>{
 const source={createdAt:'2026-10-11T12:00:00.000Z',model:'T-Deck',firmware:'test',authorFingerprint:'01'.repeat(32),authorGeneration:7,privateKey:'never-store'};
 const encrypted=await encryptConfigBackup(document,password,{...options,source});
 const result=await decryptConfigBackupDetails(encrypted,password,options);
 assert.deepEqual(result.source,{version:1,createdAt:source.createdAt,model:'T-Deck',firmware:'test',authorFingerprint:source.authorFingerprint,authorGeneration:7});
 assert.equal(Object.hasOwn(result.source,'privateKey'),false);assert.deepEqual(result.document.serialize(),document.serialize({kind:'backup'}));
 const legacy=await encryptConfigBackup(document,password,options);assert.equal((await decryptConfigBackupDetails(legacy,password,options)).source,null);
});
test('invalid provenance cannot create a backup',async()=>{
 const valid={createdAt:'2026-10-11T12:00:00.000Z',model:'T-Deck',firmware:'test'};
 for(const source of [{...valid,createdAt:'invalid'},{...valid,model:'a'.repeat(65)},{...valid,model:'line\nfeed'},
  {...valid,authorFingerprint:'01'.repeat(32)},{...valid,authorFingerprint:'01'.repeat(32),authorGeneration:0}])
  await assert.rejects(encryptConfigBackup(document,password,{...options,source}));
});
