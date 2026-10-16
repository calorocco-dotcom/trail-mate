import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {decode,encode} from '../../site/geocaching/src/protocol.js';
import {validateTrackReplicationPage,matchTrackReplicationItem} from '../../site/tracks/src/replication-page.js';
const bin=byte=>new Uint8Array(32).fill(byte);
const row=sequence=>[sequence,bin(1),bin(2),1,0,null,bin(3),100,1];

test('Python canonical inventory beyond u32 round trips without precision loss',()=>{
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv/'+(process.platform==='win32'?'Scripts/python.exe':'bin/python'));
 const native=spawnSync(python,['-m','tools.tracks.inventory_vector'],{encoding:'utf8',windowsHide:true});assert.equal(native.status,0,native.stderr);
 const source=Uint8Array.from(Buffer.from(JSON.parse(native.stdout).hex,'hex')),response=decode(source,8192),page=validateTrackReplicationPage(response);
 assert.equal(BigInt(page.high),4294967296n);assert.equal(BigInt(page.items[0].sequence),4294967296n);assert.deepEqual(encode(response),source);
 const huge=0x7fffffffffffffffn;assert.equal(validateTrackReplicationPage([huge,huge,0,[row(huge)]],{after:huge-1n}).next,huge);
});

test('frozen watermark and sequence order bind pagination including an empty window',()=>{
 assert.equal(validateTrackReplicationPage([3,1,1,[row(1)]],{limit:1}).more,true);
 assert.equal(validateTrackReplicationPage([3,3,0,[row(2),row(3)]],{after:1,high:3}).next,3);
 assert.equal(validateTrackReplicationPage([3,3,0,[]],{after:3,high:3}).items.length,0);
 for(const response of [[3,2,1,[row(1)]],[3,1,0,[row(1)]],[3,0,1,[]],[3,2,1,[row(2),row(1)]],[3,3,0,[row(4)]],[4,4,0,[row(4)]]])assert.throws(()=>validateTrackReplicationPage(response,{high:3}));
 for(const after of [-1,1.5,Number.MAX_SAFE_INTEGER+1,0x8000000000000000n])assert.throws(()=>validateTrackReplicationPage([3,3,0,[]],{after}));
});

test('unsigned inventory assertions must match the verified author manifest',()=>{
 const item=validateTrackReplicationPage([1,1,0,[row(1)]]).items[0];
 const record=[1,null,null,null,1,null,0,null,null,null,null,null,null,[bin(3),100]];
 const verified={objectId:bin(1),revisionHash:bin(2),record};assert.equal(matchTrackReplicationItem(item,verified),true);
 for(const mutate of [value=>value.objectId.fill(9),value=>value.revisionHash.fill(9),value=>value.record[4]=2,value=>value.record[6]=1,value=>value.record[5]=bin(5),value=>value.record[13][0].fill(9),value=>value.record[13][1]=101]){const bad=structuredClone(verified);mutate(bad);assert.throws(()=>matchTrackReplicationItem(item,bad));}
 const archived=row(1);archived[4]=1;assert.throws(()=>validateTrackReplicationPage([1,1,0,[archived]]));
 const missingParent=row(1);missingParent[3]=2;assert.throws(()=>validateTrackReplicationPage([1,1,0,[missingParent]]));
});
