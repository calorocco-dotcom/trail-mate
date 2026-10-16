import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {encodeTrackManifest} from '../../site/tracks/src/manifest.js';
test('actual SDK streaming SHA agrees with independent domain hashes',()=>{
 const author=new Uint8Array(64).fill(1),nonce=new Uint8Array(16).fill(2);
 const raw=encodeTrackManifest([1,author,nonce,0,1,null,0,5000000000,5000000001,'山径','说明',[0,0,0,0],[123,null,null,2],[new Uint8Array(32).fill(3),300,'application/vnd.google-earth.kml+xml',1]]);
 const exe=path.resolve('.codex-build/hostlink-codec/native/track_manifest_hash'+(process.platform==='win32'?'.exe':''));const result=execFileSync(exe,[Buffer.from(raw).toString('hex')],{encoding:'utf8'}).trim().split(/\r?\n/);
 const digest=(domain,...parts)=>createHash('sha256').update(domain+'\0').update(Buffer.concat(parts.map(part=>Buffer.from(part)))).digest('hex');
 assert.deepEqual(result,[digest('TrailMate.Track.ID.v1',author,nonce),digest('TrailMate.Track.Revision.v1',raw)]);
});
