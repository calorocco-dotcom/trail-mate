import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {encodeTrackManifest} from '../../site/tracks/src/manifest.js';
import {encode} from '../../site/geocaching/src/protocol.js';
const exe=path.resolve('.codex-build/hostlink-codec/native/track_manifest'+(process.platform==='win32'?'.exe':''));
const record=()=>[1,new Uint8Array(64).fill(1),new Uint8Array(16).fill(2),0,1,null,0,5000000000,5000000001,'山径','说明',[1700000000,-10000000,-1700000000,10000000],[123,null,0,100000],[new Uint8Array(32).fill(3),8388608,'application/vnd.google-earth.kml+xml',1]];
const run=raw=>{const result=spawnSync(exe,[Buffer.from(raw).toString('hex')],{encoding:'utf8'});if(result.error)throw result.error;return result;};
test('C++ accepts browser bytes, u64 timestamp, dateline and exact point/file budgets',()=>{const result=run(encodeTrackManifest(record()));assert.equal(result.status,0);assert.equal(result.stdout.trim(),'5000000000,100000');});
test('C++ and browser reject the same invalid fields and truncated/trailing bytes',()=>{
 for(const change of [r=>r[9]='a\0',r=>r[6]=1,r=>r[5]=new Uint8Array(32),r=>r[12][0]=1.5,r=>r[12][3]=100001,r=>r[13][1]=8388609,r=>r[8]=4999999999,r=>r[11][1]=900000001]){const value=record();change(value);assert.throws(()=>encodeTrackManifest(value));assert.equal(run(encode(value)).status,1);}
 const raw=encodeTrackManifest(record());assert.equal(run(raw.slice(0,-1)).status,1);assert.equal(run(new Uint8Array([...raw,0])).status,1);
});
