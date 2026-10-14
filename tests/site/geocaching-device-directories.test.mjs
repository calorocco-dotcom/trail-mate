import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {readDeviceDirectories} from '../../site/geocaching/src/device-directories.js';
const identity=await Identity.generate(),key=await identity.getPublicKey(),utf8=new TextEncoder();
async function address(aspect){return Identity.truncatedHash(Uint8Array.from([...(await Identity.fullHash(utf8.encode(aspect))).slice(0,10),...identity.identityHash]));}
const row=[await address('lxmf.delivery'),await address('trailmate.geocache.directory'),key,new Uint8Array(16),1,'Directory',1,20,1500];
test('device directories bind both service addresses to the supplied public identity',async()=>{
  const calls=[];const session={async request(op,[offset]){calls.push([op,offset]);return [offset,1,offset?null:row];}};
  const result=await readDeviceDirectories(session);assert.equal(result.length,1);assert.equal(result[0].capabilitiesVerified,true);assert.equal(result[0].ageMs,1500);
  assert.deepEqual(calls,[[70,0],[70,1]]);
});
test('wrong address, duplicate directory and false end markers are refused',async()=>{
  await assert.rejects(readDeviceDirectories({async request(){return [0,1,[new Uint8Array(16),...row.slice(1)]];}}),/地址不一致/);
  await assert.rejects(readDeviceDirectories({async request(op,[offset]){return [offset,2,row];}}),/重复地址/);
  await assert.rejects(readDeviceDirectories({async request(){return [0,1,null];}}),/结束标记/);
});
test('empty discovery is bounded to one response while Busy has a finite deadline',async()=>{
  let calls=0;assert.deepEqual(await readDeviceDirectories({async request(){calls++;return [0,0,null];}}),[]);assert.equal(calls,1);
  let clock=0;const busy={async request(){const e=Error('busy');e.code=5;throw e;}};
  await assert.rejects(readDeviceDirectories(busy,{timeoutMs:200,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/尚未就绪/);
});

test('target selection waits through empty discovery and unverified capabilities',async()=>{
  let attempts=0,clock=0;
  const session={async request(op,[offset]){if(!offset){attempts++;return [0,attempts===1?0:1,attempts===1?null:[...row.slice(0,6),attempts===2?0:1,attempts===2?0:20,row[8]]];}return [offset,1,null];}};
  const result=await readDeviceDirectories(session,{waitForReadyTarget:true,now:()=>clock,sleep:async ms=>{clock+=ms;}});
  assert.equal(result.length,1);assert.equal(result[0].capabilitiesVerified,true);assert.equal(attempts,3);
});
