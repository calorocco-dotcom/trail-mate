import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage,proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';

const executable=path.resolve('.codex-build/hostlink-codec/native/device_author'+(process.platform==='win32'?'.exe':''));
const vector=JSON.parse(execFileSync(executable,{encoding:'utf8'}));
const fromHex=value=>Uint8Array.from(Buffer.from(value,'hex'));
const publicKey=fromHex(vector.publicKey),signature=fromHex(vector.signature),message=fromHex(vector.message);
const sessionId=new Uint8Array(16).fill(1),challenge=new Uint8Array(32).fill(2);

test('firmware proof encoder and Ed25519 signature interoperate with browser verifier',async()=>{
  assert.deepEqual(message,authorProofMessage(sessionId,challenge,publicKey,7));
  const identity=await Identity.fromPublicKey(publicKey);
  assert.equal(await identity.validate(signature,message),true);
  const name=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
  const destination=await Identity.truncatedHash(Uint8Array.from([...name,...identity.identityHash]));
  const info=[publicKey,identity.identityHash,destination,7];
  const session={id:sessionId,async request(op){return op===OPERATION.AUTHOR_INFO?info:[...info,signature];}};
  const author=await proveDeviceAuthor(session,{randomBytes:()=>challenge});
  assert.equal(author.verified,true);
  const otherChallenge=challenge.slice();otherChallenge[0]^=1;
  assert.equal(await identity.validate(signature,authorProofMessage(sessionId,otherChallenge,publicKey,7)),false);
  const swappedEncryptionKey=publicKey.slice();swappedEncryptionKey[0]^=1;
  assert.equal(await identity.validate(signature,authorProofMessage(sessionId,challenge,swappedEncryptionKey,7)),false);
});
