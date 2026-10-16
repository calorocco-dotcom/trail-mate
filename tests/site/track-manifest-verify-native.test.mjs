import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
const exe=path.resolve('.codex-build/hostlink-codec/native/track_manifest_verify'+(process.platform==='win32'?'.exe':''));
const run=raw=>{const result=spawnSync(exe,[Buffer.from(raw).toString('hex')],{encoding:'utf8'});if(result.error)throw result.error;return result;};
test('web Ed25519 signature verifies in C++ with actual SHA hashes',async()=>{
 const identity=await Identity.generate(),author=await identity.getPublicKey();const raw=encodeTrackManifest([1,author,new Uint8Array(16).fill(2),0,1,null,0,5000000000,5000000001,'山径','说明',[0,0,0,0],[123,null,null,2],[new Uint8Array(32).fill(3),300,'application/vnd.google-earth.kml+xml',1]]);
 const signature=await identity.sign(trackManifestSigningBytes(raw)),web=await verifyTrackManifest([raw,signature]);
 const result=run(encode([raw,signature]));assert.equal(result.status,0);assert.deepEqual(result.stdout.trim().split(/\r?\n/),[web.objectId,web.revisionHash].map(bytes=>Buffer.from(bytes).toString('hex')));
 const changed=raw.slice();changed[4]^=1;assert.equal(run(encode([changed,signature])).status,1);
 const badSignature=signature.slice();badSignature[0]^=1;assert.equal(run(encode([raw,badSignature])).status,1);
 const otherSignature=await identity.sign(raw);assert.equal(run(encode([raw,otherSignature])).status,1);
 const signed=encode([raw,signature]);assert.equal(run(signed.slice(0,-1)).status,1);assert.equal(run(new Uint8Array([...signed,0])).status,1);
});
