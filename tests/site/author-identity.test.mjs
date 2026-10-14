import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage,validateAuthorInfo,proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';

const identity=await Identity.generate(),publicKey=await identity.getPublicKey();
const nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
const destination=await Identity.truncatedHash(Uint8Array.from([...nameHash,...identity.identityHash]));
const info=[publicKey,identity.identityHash,destination,7],sessionId=new Uint8Array(16).fill(1),nonce=new Uint8Array(32).fill(2);

function session(signing=identity,{signedSession=sessionId,signedNonce=nonce,replyInfo=info}={}){
  return {id:sessionId,async request(op,body){
    if(op===OPERATION.AUTHOR_INFO){assert.deepEqual(body,[]);return info;}
    assert.equal(op,OPERATION.AUTHOR_PROVE);assert.deepEqual(body,[nonce,7]);
    return [...replyInfo,await signing.sign(authorProofMessage(signedSession,signedNonce,replyInfo[0],replyInfo[3]))];
  }};
}

test('fixed proof wire binds domain, session, nonce, complete public key and generation',()=>{
  const message=authorProofMessage(sessionId,nonce,new Uint8Array(64).fill(3),7);
  const expected=Uint8Array.from([...new TextEncoder().encode('trailmate.hostlink.author/v1\0'),0x95,1,0xc4,16,...sessionId,0xc4,32,...nonce,0xc4,64,...new Uint8Array(64).fill(3),7]);
  assert.deepEqual(message,expected);
  assert.throws(()=>authorProofMessage(sessionId,nonce,publicKey,0));
});
test('public author facts alone are not a proof; only real device-key signature verifies',async()=>{
  const facts=await validateAuthorInfo(info);assert.equal(facts.verified,undefined);
  const proven=await proveDeviceAuthor(session(),{randomBytes:()=>nonce});
  assert.equal(proven.verified,true);assert.equal(proven.fingerprint.length,64);
  assert.deepEqual(proven.destination,destination);
  proven.publicKey[0]^=1;assert.deepEqual(info[0],publicKey);
});
test('a proof from another session, challenge or private key is rejected',async()=>{
  const options={randomBytes:()=>nonce};
  await assert.rejects(proveDeviceAuthor(session(identity,{signedSession:new Uint8Array(16)}),options),/验签失败/);
  await assert.rejects(proveDeviceAuthor(session(identity,{signedNonce:new Uint8Array(32)}),options),/验签失败/);
  await assert.rejects(proveDeviceAuthor(session(await Identity.generate()),options),/验签失败/);
});
test('identity hash, destination and generation cannot change during proof',async()=>{
  await assert.rejects(validateAuthorInfo([publicKey,new Uint8Array(16),destination,7]),/不一致/);
  await assert.rejects(validateAuthorInfo([publicKey,identity.identityHash,new Uint8Array(16),7]),/不一致/);
  await assert.rejects(proveDeviceAuthor(session(identity,{replyInfo:[...info.slice(0,3),8]}),{randomBytes:()=>nonce}),/发生变化/);
});
test('a paused verification sends no new request and never returns a late proof',async()=>{
 const before=new AbortController();before.abort();let calls=0;
 await assert.rejects(proveDeviceAuthor({id:sessionId,request(){++calls;}},{signal:before.signal}),{name:'AbortError'});
 assert.equal(calls,0);
 for(const heldOperation of [OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE]){
  const abort=new AbortController(),seen=[];let resume,entered;
  const waiting=new Promise(resolve=>entered=resolve);
  const signed=session();
  const pending=proveDeviceAuthor({id:sessionId,async request(op,body){seen.push(op);if(op===heldOperation){entered();await new Promise(resolve=>resume=resolve);}return signed.request(op,body);}},{randomBytes:()=>nonce,signal:abort.signal});
  const rejected=assert.rejects(pending,{name:'AbortError'});
  await waiting;abort.abort();resume();await rejected;
  assert.deepEqual(seen,heldOperation===OPERATION.AUTHOR_INFO?[OPERATION.AUTHOR_INFO]:[OPERATION.AUTHOR_INFO,OPERATION.AUTHOR_PROVE]);
 }
});
