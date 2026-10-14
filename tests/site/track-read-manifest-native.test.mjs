import test from 'node:test';import assert from 'node:assert/strict';import path from 'node:path';import {spawnSync} from 'node:child_process';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
const executable=path.resolve('.codex-build/hostlink-codec/native/track_read_manifest'+(process.platform==='win32'?'.exe':''));
const identity=await Identity.generate(),author=await identity.getPublicKey(),request=new Uint8Array(16).fill(2);
const record=[1,author,new Uint8Array(16).fill(7),0,1,null,0,100,100,'Trail','',[0,0,1,1],[0,null,null,2],[new Uint8Array(32).fill(3),200,'application/vnd.google-earth.kml+xml',1]];
const raw=encodeTrackManifest(record),signed=[raw,await identity.sign(trackManifestSigningBytes(raw))],verified=await verifyTrackManifest(signed);
const response=encode([1,1,2,request,200,[signed,1]]),hex=value=>Buffer.from(value).toString('hex');
const run=(bytes=response,id=request,object=verified.objectId,revision=verified.revisionHash)=>spawnSync(executable,[hex(bytes),hex(id),hex(object),hex(revision)],{encoding:'utf8',windowsHide:true});
test('actual native Ed25519 and SHA-256 accept browser-signed exact and current references',()=>{assert.equal(run().status,0);assert.equal(run(response,request,verified.objectId,new Uint8Array()).status,0);});
test('wrong request/object/version, tampered signature and trailing bytes fail closed',()=>{
 for(const result of [run(response,new Uint8Array(16)),run(response,request,new Uint8Array(32)),run(response,request,verified.objectId,new Uint8Array(32)),run(Uint8Array.from([...response,0]))])assert.equal(result.status,1);
 const signature=signed[1].slice();signature[0]^=1;assert.equal(run(encode([1,1,2,request,200,[[raw,signature],1]])).status,1);
});
