import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {decode} from '../../site/geocaching/src/protocol.js';
test('native snapshot response decodes with browser canonical MessagePack implementation',()=>{
 const executable=path.resolve('.codex-build/hostlink-codec/native/track_snapshot'+(process.platform==='win32'?'.exe':''));
 const hex=execFileSync(executable,{encoding:'utf8'}).trim();assert.match(hex,/^[0-9a-f]+$/);
 const body=decode(new Uint8Array(Buffer.from(hex,'hex')),512);
 assert.equal(body.length,6);assert.deepEqual(body[0],new Uint8Array(16).fill(7));assert.equal(body[1],700);
 assert.deepEqual(body[2],new Uint8Array(32).fill(8));assert.equal(body[3],2);assert.deepEqual(body[4],new Uint8Array(320).fill(0x55));assert.equal(body[5],0);
});
