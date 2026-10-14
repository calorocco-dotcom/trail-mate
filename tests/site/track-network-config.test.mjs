import test from 'node:test';
import assert from 'node:assert/strict';
import {parseTrackNetworkConfig} from '../../site/tracks/src/network-config.js';

test('shared gateway keeps Geocaching and track discovery identities separate',()=>{
 const geo='ab'.repeat(16),tracks=['01'.repeat(16),'A2'.repeat(16)];
 const result=parseTrackNetworkConfig({endpoint:'wss://example.test',discoverySeeds:[geo],trackDiscoverySeeds:tracks});
 assert.equal(result.endpoint,'wss://example.test/');
 assert.deepEqual(result.discoverySeeds,[tracks[0],tracks[1].toLowerCase()]);
 assert.deepEqual(parseTrackNetworkConfig({endpoint:'wss://example.test/',discoverySeeds:[geo]}).discoverySeeds,[]);
 tracks[0]='ff'.repeat(16);assert.equal(result.discoverySeeds[0],'01'.repeat(16));
 assert.ok(Object.isFrozen(result)&&Object.isFrozen(result.discoverySeeds));
});

test('invalid operator track hints fail together instead of silently applying a partial list',()=>{
 for(const seeds of [null,'01'.repeat(16),['01'.repeat(15)],['g1'.repeat(16)],[17],Array(4).fill('01'.repeat(16))]){
  if(seeds===null)continue; // null/absent retains announcement-only compatibility.
  assert.throws(()=>parseTrackNetworkConfig({endpoint:'wss://example.test/',trackDiscoverySeeds:seeds}));
 }
 assert.deepEqual(parseTrackNetworkConfig({endpoint:'wss://example.test/',trackDiscoverySeeds:['AB'.repeat(16),'ab'.repeat(16)]}).discoverySeeds,['ab'.repeat(16)]);
});

test('network config accepts only WSS or explicit loopback WS without URL credentials',()=>{
 for(const endpoint of ['https://example.test/','ws://example.test/','wss://user:secret@example.test/','wss://example.test/#fragment','not a URL'])assert.throws(()=>parseTrackNetworkConfig({endpoint}));
 for(const endpoint of ['wss://example.test/','ws://localhost:8787/','ws://127.0.0.1:8787/','ws://[::1]:8787/'])assert.equal(parseTrackNetworkConfig({endpoint}).endpoint,endpoint);
 for(const config of [null,[],{}, {endpoint:4}])assert.throws(()=>parseTrackNetworkConfig(config));
});
