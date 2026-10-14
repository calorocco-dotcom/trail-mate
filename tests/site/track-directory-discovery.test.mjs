import test from 'node:test';
import assert from 'node:assert/strict';
import {parseDirectoryDiscoveryAddress,requestTrackDirectoryDiscovery} from '../../site/tracks/src/directory-discovery.js';
test('invalid discovery addresses expose stable localization codes',()=>{
 assert.throws(()=>parseDirectoryDiscoveryAddress('bad'),{code:'DIRECTORY_ADDRESS_INVALID'});
 assert.throws(()=>parseDirectoryDiscoveryAddress('00'.repeat(16)),{code:'DIRECTORY_ADDRESS_ZERO'});
});
test('directory address accepts plain and Reticulum pretty hex while rejecting invalid input',()=>{
  const address='0123456789abcdef0123456789abcdef';
  assert.deepEqual(parseDirectoryDiscoveryAddress(' <'+address.toUpperCase()+'> '),parseDirectoryDiscoveryAddress(address));
  for(const value of ['',address+'00','0'.repeat(32),'g'.repeat(32)])assert.throws(()=>parseDirectoryDiscoveryAddress(value));
});
test('path request uses operation 90 and means requested, without asserting a verified directory',async()=>{
  const input=new Uint8Array(16).fill(7),calls=[];
  const result=await requestTrackDirectoryDiscovery({async request(op,body){calls.push([op,body[0].slice()]);input.fill(9);return []; }},input);
  assert.equal(result.requested,true);assert.equal(result.verified,undefined);assert.equal(result.discovery[0],7);
  assert.equal(calls[0][0],90);assert.equal(calls[0][1][0],7);
  await assert.rejects(requestTrackDirectoryDiscovery({async request(){return [1];}},input),/响应无效/);
});
test('busy retries preserve the address and stop at deadline or cancellation',async()=>{
  const target=new Uint8Array(16).fill(2);let clock=0;
  await assert.rejects(requestTrackDirectoryDiscovery({async request(){const error=Error('busy');error.code=5;throw error;}},target,{timeoutMs:2000,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/尚未就绪/);
  const controller=new AbortController();
  await assert.rejects(requestTrackDirectoryDiscovery({async request(){controller.abort();return []; }},target,{signal:controller.signal}),/abort/i);
});
