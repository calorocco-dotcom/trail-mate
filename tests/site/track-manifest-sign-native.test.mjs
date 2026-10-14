import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {verifyTrackManifest} from '../../site/tracks/src/manifest.js';
test('native Ed25519 track manifest is accepted by web verifier',async()=>{
 const exe=path.resolve('.codex-build/hostlink-codec/native/track_manifest_sign'+(process.platform==='win32'?'.exe':''));
 const lines=execFileSync(exe,{encoding:'utf8'}).trim().split(/\r?\n/);assert.equal(lines.length,2);
 const raw=Uint8Array.from(Buffer.from(lines[0],'hex')),signature=Uint8Array.from(Buffer.from(lines[1],'hex'));
 const verified=await verifyTrackManifest([raw,signature]);assert.equal(verified.record[9],'山径');assert.equal(verified.record[7],5000000000);assert.equal(verified.objectId.length,32);
 const changed=signature.slice();changed[0]^=1;await assert.rejects(verifyTrackManifest([raw,changed]),/签名验证失败/);
});
