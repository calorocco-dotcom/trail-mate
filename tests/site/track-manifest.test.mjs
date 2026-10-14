import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest,verifyTrackSuccessor} from '../../site/tracks/src/manifest.js';
const identity=await Identity.generate(),author=await identity.getPublicKey();
const record=()=>[1,author.slice(),new Uint8Array(16).fill(2),0,1,null,0,100,100,'山径','说明',[1700000000,-10000000,-1700000000,10000000],[100,null,null,2],[new Uint8Array(32).fill(3),300,'application/vnd.google-earth.kml+xml',1]];
const sign=async value=>{const raw=encodeTrackManifest(value);return verifyTrackManifest([raw,await identity.sign(trackManifestSigningBytes(raw))]);};
test('frozen canonical vector includes u64 timestamps',()=>{
 const value=[1,new Uint8Array(64).fill(1),new Uint8Array(16).fill(2),0,1,null,0,5000000000,5000000000,'t','',[0,0,0,0],[0,null,null,1],[new Uint8Array(32).fill(3),1,'application/vnd.google-earth.kml+xml',1]];
 assert.equal(Buffer.from(encodeTrackManifest(value)).toString('hex'),'9e01c44001010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101c410020202020202020202020202020202020001c000cf000000012a05f200cf000000012a05f200a174a094000000009400c0c00194c420030303030303030303030303030303030303030303030303030303030303030301d9246170706c69636174696f6e2f766e642e676f6f676c652d65617274682e6b6d6c2b786d6c01');
});
test('real author signature and independent domain hashes bind exact bytes',async()=>{
 const raw=encodeTrackManifest(record()),signature=await identity.sign(trackManifestSigningBytes(raw));const signed=await verifyTrackManifest([raw,signature],{expectedAuthor:author});
 const hash=(domain,...parts)=>new Uint8Array(createHash('sha256').update(domain+'\0').update(Buffer.concat(parts.map(part=>Buffer.from(part)))).digest());
 assert.deepEqual(signed.objectId,hash('TrailMate.Track.ID.v1',author,record()[2]));assert.deepEqual(signed.revisionHash,hash('TrailMate.Track.Revision.v1',raw));
 const mutated=raw.slice();mutated[mutated.length-1]^=1;await assert.rejects(verifyTrackManifest([mutated,signature]));
 await assert.rejects(verifyTrackManifest([raw,signature],{expectedAuthor:new Uint8Array(64)}),/身份/);
});
test('input mutation after call cannot replace author or signed payload',async()=>{
 const raw=encodeTrackManifest(record()),signature=await identity.sign(trackManifestSigningBytes(raw)),expected=author.slice();const pending=verifyTrackManifest([raw,signature],{expectedAuthor:expected});raw.fill(0);signature.fill(0);expected.fill(0);assert.deepEqual((await pending).record[1],author);
});
test('revision chain validates predecessor and archive preserves file',async()=>{
 const first=await sign(record()),secondRecord=record();secondRecord[4]=2;secondRecord[5]=first.revisionHash;secondRecord[8]=101;const second=await sign(secondRecord);assert.equal(verifyTrackSuccessor(first,second),true);
 const archive=record();archive[4]=3;archive[5]=second.revisionHash;archive[6]=1;archive[8]=102;const archived=await sign(archive);assert.equal(verifyTrackSuccessor(second,archived),true);
 archive[13][0][0]^=1;assert.throws(()=>verifyTrackSuccessor(second,{...archived}),/先验证/);assert.throws(()=>verifyTrackSuccessor(second,first),/不一致/);
 const changed=await sign(archive);assert.throws(()=>verifyTrackSuccessor(second,changed),/保留原文件/);
 // Exposed arrays cannot rewrite the internally verified chain evidence.
 second.record[4]=99;second.revisionHash.fill(0);assert.equal(verifyTrackSuccessor(first,second),true);
});
test('schema rejects unsafe text, float stats, bad bounds and file limits',()=>{
 for(const change of [r=>r[6]=1,r=>r[5]=new Uint8Array(32),r=>r[9]='a\0',r=>r[11][1]=900000001,r=>r[12][0]=1.5,r=>r[12][3]=100001,r=>r[13][1]=8388609,r=>r[13][3]=2,r=>r[8]=99]){const value=record();change(value);assert.throws(()=>encodeTrackManifest(value));}
});
