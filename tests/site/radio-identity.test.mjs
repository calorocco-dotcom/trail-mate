import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {OPERATION,encodeManagement,ManagementSession,decodeManagement} from '../../site/device/src/management-protocol.js';
import {FRAME,CAPABILITY} from '../../site/device/src/hostlink.js';
import {decode,encode} from '../../site/geocaching/src/protocol.js';
import {encodeRadioIdentityBackupPayload,decodeRadioIdentityBackupPayload,radioIdentityBackupSigningBytes,verifyRadioIdentityBackupPayload,
 encryptRadioIdentityBackup,decryptRadioIdentityBackup,clearRadioIdentityBackup,exportEncryptedRadioIdentity,restoreRadioIdentityBackup,verifyRestoredRadioIdentity} from '../../site/device/src/radio-identity.js';
const operations=[1,2,7,8,9],password='radio identity archive password';
test('management consent accepts canonical booleans while Geocaching keeps rejecting them',()=>{
 assert.throws(()=>decode(encode([true,false])),/Forbidden CMP1 type/);
 for(const operation of [4,5,8,9])for(const consent of [false,true]){
  const raw=encodeManagement(new Uint8Array(16),new Uint8Array(16),operation,[3,consent]);
  assert.deepEqual(decodeManagement(raw).body,[3,consent]);
 }
});
async function fixture(kind=1){
 const signer=await Identity.generate(),authorPublic=await signer.getPublicKey(),prefix=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),destination=await Identity.truncatedHash(Uint8Array.from([...prefix,...signer.identityHash]));
 const privateKey=new Uint8Array(kind===1?64:32).fill(7);if(kind===2){privateKey[0]&=0xf8;privateKey[31]=(privateKey[31]&0x3f)|0x40;}
 const payload=encodeRadioIdentityBackupPayload({kind,publicKey:new Uint8Array(32).fill(5),privateKey,sourceAuthor:authorPublic,authorGeneration:3,radioGeneration:1,exportSession:new Uint8Array(16).fill(2),exportRequest:new Uint8Array(16).fill(3)}),signed=[payload,await signer.sign(radioIdentityBackupSigningBytes(payload))],calls=[];
 let radio={kind,present:true,generation:5,publicKey:new Uint8Array(32).fill(9)},mode=0;
 const session={id:new Uint8Array(16).fill(2),async request(operation,body,options={}){calls.push(operation);const info=[authorPublic,signer.identityHash,destination,3];
  if(operation===1)return info;if(operation===2)return [...info,await signer.sign(authorProofMessage(this.id,body[0],authorPublic,3))];
  if(operation===7)return [kind,radio.present?1:0,radio.generation,radio.publicKey];
  if(operation===8){assert.deepEqual(body,[3,kind,true]);if(mode==='replay')return signed;const value=decodeRadioIdentityBackupPayload(signed[0]);signed[0]=encodeRadioIdentityBackupPayload({...value,exportSession:this.id,exportRequest:options.requestId});value.privateKey.fill(0);signed[1]=await signer.sign(radioIdentityBackupSigningBytes(signed[0]));return signed;}
  assert.equal(operation,9);assert.equal(body[0],3);assert.equal(body[4],radio.generation);assert.deepEqual(body[5],radio.publicKey);assert.equal(body[6],true);
  const value=decodeRadioIdentityBackupPayload(body[2]);const pub=value.publicKey;value.privateKey.fill(0);radio={kind,present:true,generation:radio.generation+1,publicKey:pub};
  if(mode==='timeout')throw Object.assign(Error('lost response'),{code:'TIMEOUT'});return [mode,kind,radio.generation,pub];
 }};
 return {signed,author:{verified:true,publicKey:authorPublic,generation:3},session,calls,get current(){return {...radio,publicKey:radio.publicKey?.slice()};},set mode(value){mode=value;}};
}
test('two native RF representations encrypt, authenticate and explicitly clear plaintext',async()=>{
 for(const kind of [1,2]){const f=await fixture(kind),file=await encryptRadioIdentityBackup(f.signed,f.author,password);assert.ok(file.length<=504);assert.equal(new TextDecoder().decode(file.slice(0,8)),'TMRFENC1');
  const decoded=await decryptRadioIdentityBackup(file,password);assert.equal(decoded.identity.kind,kind);assert.equal(decoded.identity.privateKey.length,kind===1?64:32);assert.deepEqual(decoded.signed,f.signed);clearRadioIdentityBackup(decoded);assert.ok(decoded.identity.privateKey.every(byte=>byte===0));assert.ok(decoded.signed[0].every(byte=>byte===0));}
});
test('bad proof, another author, archive tampering and hostile derivation costs are rejected',async()=>{
 const f=await fixture(),bad=[f.signed[0].slice(),f.signed[1].slice()];bad[1][0]^=1;await assert.rejects(verifyRadioIdentityBackupPayload(bad,f.author),e=>e.code==='RADIO_IDENTITY_SIGNATURE_INVALID');
 await assert.rejects(verifyRadioIdentityBackupPayload(f.signed,{...f.author,generation:4}),e=>e.code==='RADIO_IDENTITY_AUTHOR_CHANGED');
 const file=await encryptRadioIdentityBackup(f.signed,f.author,password);await assert.rejects(decryptRadioIdentityBackup(file,'wrong password'),e=>e.code==='IDENTITY_BACKUP_AUTH_FAILED');
 const changed=file.slice();changed[12]^=1;await assert.rejects(decryptRadioIdentityBackup(changed,password),e=>e.code==='IDENTITY_BACKUP_AUTH_FAILED');
 const cost=file.slice();new DataView(cost.buffer).setUint32(8,0xffffffff,false);await assert.rejects(decryptRadioIdentityBackup(cost,password),e=>e.code==='IDENTITY_BACKUP_PARAMETERS_UNSUPPORTED');
});
test('a valid old RF export cannot satisfy a fresh USB request',async()=>{const f=await fixture();f.mode='replay';await assert.rejects(exportEncryptedRadioIdentity(f.session,1,password,{operations,consent:true}),error=>error.code==='RADIO_IDENTITY_EXPORT_CONTEXT_CHANGED');assert.deepEqual(f.calls,[1,2,8]);assert.ok(f.signed[0].every(byte=>byte===0));});
test('Buffer inputs freeze before asynchronous verification and USB export returns only encrypted bytes',async()=>{
 const f=await fixture(),signed=f.signed.map(value=>Buffer.from(value)),pending=verifyRadioIdentityBackupPayload(signed,f.author);signed.forEach(value=>value.fill(0));const decoded=await pending;assert.equal(decoded.privateKey[0],7);decoded.privateKey.fill(0);
 const exported=await exportEncryptedRadioIdentity(f.session,1,password,{operations,consent:true});assert.deepEqual(f.calls,[1,2,8]);assert.equal(Object.hasOwn(exported,'privateKey'),false);assert.ok(f.signed[0].every(byte=>byte===0));
 const restored=await decryptRadioIdentityBackup(exported.bytes,password);assert.equal(restored.identity.kind,1);clearRadioIdentityBackup(restored);
});
test('restore pins both identities, sends once, preserves the account and verifies after reboot',async()=>{
 for(const mode of [0,1,'timeout']){const f=await fixture(2),file=await encryptRadioIdentityBackup(f.signed,f.author,password),decoded=await decryptRadioIdentityBackup(file,password);f.mode=mode;
  let outcome;try{outcome=await restoreRadioIdentityBackup(f.session,decoded,f.author,f.current,{operations,consent:true});assert.notEqual(mode,'timeout');}
  catch(error){assert.equal(mode,'timeout');assert.equal(error.code,'RADIO_IDENTITY_OUTCOME_UNKNOWN');outcome={expectation:error.expectation};}
  assert.equal(outcome.expectation.authorGeneration,3);assert.equal(outcome.expectation.generation,6);assert.deepEqual(f.calls,[1,2,7,9]);assert.ok(decoded.identity.privateKey.every(byte=>byte===0));
  await verifyRestoredRadioIdentity(f.session,outcome.expectation,{operations});assert.deepEqual(f.calls,[1,2,7,9,1,2,7]);assert.equal(f.calls.filter(op=>op===9).length,1);
 }
});
test('unsupported capability, invalid password and mismatched draft metadata produce no private USB traffic',async()=>{
 const f=await fixture();await assert.rejects(exportEncryptedRadioIdentity(f.session,1,password,{operations:[],consent:true}),e=>e.code==='RADIO_IDENTITY_UNSUPPORTED');
 await assert.rejects(exportEncryptedRadioIdentity(f.session,1,'short',{operations,consent:true}),e=>e.code==='IDENTITY_BACKUP_PASSWORD_INVALID');assert.equal(f.calls.length,0);
 const file=await encryptRadioIdentityBackup(f.signed,f.author,password),decoded=await decryptRadioIdentityBackup(file,password);decoded.identity.publicKey[0]^=1;
 await assert.rejects(restoreRadioIdentityBackup(f.session,decoded,f.author,f.current,{operations,consent:true}),e=>e.code==='RADIO_IDENTITY_PAYLOAD_INVALID');assert.equal(f.calls.length,0);
});
test('RF restore fits the existing frame and marks encoded material sensitive on failure',async()=>{
 const f=await fixture(),body=[3,f.author.publicKey,f.signed[0],f.signed[1],5,new Uint8Array(32).fill(9),true];assert.ok(encodeManagement(new Uint8Array(16),new Uint8Array(16),9,body).length<=512);
 let listener,retained;const connection={state:'ready',hello:{capabilities:CAPABILITY.MANAGEMENT},subscribe(fn){listener=fn;return()=>{};},async request(type,payload,options){retained=payload;assert.equal(options.sensitive,true);const decoded=decodeManagement(payload);listener({type:'frame',frame:{type:FRAME.MANAGEMENT_RESPONSE,payload:encodeManagement(decoded.session,decoded.request,5,[])}});}};
 const session=new ManagementSession(connection);await assert.rejects(session.request(OPERATION.RADIO_IDENTITY_BACKUP_RESTORE,body),e=>e.code===5);assert.ok(retained.every(byte=>byte===0));session.close();
});
