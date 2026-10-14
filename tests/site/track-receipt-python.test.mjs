import test from 'node:test';import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';import path from 'node:path';
import {verifyTrackReceipt} from '../../site/tracks/src/receipt.js';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
const vector=JSON.parse(execFileSync(python,['-m','tools.tracks.receipt_vector'],{encoding:'utf8'}));
const bin=hex=>new Uint8Array(Buffer.from(hex,'hex'));
const fixture=()=>({signed:vector.receipt.map(bin),context:{directoryPublicKey:bin(vector.directory),directoryEpoch:bin(vector.epoch),requestId:bin(vector.request),manifestSigned:vector.manifest.map(bin)}});
test('browser accepts Python RNS receipt for native C++ manifest',async()=>{
 const f=fixture(),result=await verifyTrackReceipt(f.signed,f.context);
 assert.equal(result.directoryConfirmed,true);assert.equal(result.acceptedAt,5000000000);assert.equal(result.objectId.length,32);
});
test('wrong request, epoch, directory and changed signature are rejected',async()=>{
 for(const field of ['requestId','directoryEpoch','directoryPublicKey']){const f=fixture();f.context[field][0]^=1;await assert.rejects(verifyTrackReceipt(f.signed,f.context));}
 const f=fixture();f.signed[1][0]^=1;await assert.rejects(verifyTrackReceipt(f.signed,f.context),/签名验证失败/);
});
test('mutating inputs after verification starts cannot replace context',async()=>{
 const f=fixture(),pending=verifyTrackReceipt(f.signed,f.context);
 f.signed.forEach(value=>value.fill(0));for(const key of ['requestId','directoryEpoch','directoryPublicKey'])f.context[key].fill(0);
 f.context.manifestSigned.forEach(value=>value.fill(0));
 assert.equal((await pending).directoryConfirmed,true);
});
