import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {readTrackDirectories} from '../../site/tracks/src/device-directories.js';
const identity=await Identity.generate(),key=await identity.getPublicKey(),utf8=new TextEncoder();
const delivery=await Identity.truncatedHash(Uint8Array.from([...(await Identity.fullHash(utf8.encode('lxmf.delivery'))).slice(0,10),...identity.identityHash]));
const row=[delivery,key,new Uint8Array(16).fill(3),'Track directory',0,0,0,0];
test('track directory operation preserves announced identity and unverified status',async()=>{
  const calls=[];
  const rows=await readTrackDirectories({async request(op,body){calls.push([op,body]);return [null,[row]];}});
  assert.deepEqual(calls,[[89,[0]]]);assert.equal(rows.length,1);assert.equal(rows[0].capabilitiesVerified,false);
  assert.deepEqual(rows[0].publicKey,key);assert.equal(rows[0].protocol,'trailmate.track.v1');
});
test('wrong address, contradictory capability limits and duplicate entries fail closed',async()=>{
  await assert.rejects(readTrackDirectories({async request(){return [null,[[new Uint8Array(16).fill(7),...row.slice(1)]]];}}),/地址不一致/);
  await assert.rejects(readTrackDirectories({async request(){return [null,[[...row.slice(0,5),1,0,0]]];}}),/格式无效/);
  await assert.rejects(readTrackDirectories({async request(op,[cursor]){return [cursor?null:1,[row]];}}),/重复地址/);
  await assert.rejects(readTrackDirectories({async request(){return [1,[]];}}),/分页响应无效/);
});
test('empty discovery and busy retries have bounded completion',async()=>{
  assert.deepEqual(await readTrackDirectories({async request(){return [null,[]];}}),[]);
  let clock=0;
  await assert.rejects(readTrackDirectories({async request(){const error=Error('busy');error.code=5;throw error;}},{timeoutMs:200,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/尚未就绪/);
  const controller=new AbortController();
  await assert.rejects(readTrackDirectories({async request(){controller.abort();return [null,[row]];}},{signal:controller.signal}),/abort/i);
});
