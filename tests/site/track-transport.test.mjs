import test from 'node:test';import assert from 'node:assert/strict';
import {normalizeTrackTransportEndpoint} from '../../site/tracks/src/transport-identity.js';
import {trackTransportText} from '../../site/i18n/track-transport-labels.js';import {locales} from '../../site/i18n/registry.js';
test('transport storage uses the canonical complete endpoint and rejects unsafe schemes/credentials/fragments',()=>{
 assert.equal(normalizeTrackTransportEndpoint('wss://EXAMPLE.test:443'),'wss://example.test/');assert.equal(normalizeTrackTransportEndpoint('ws://localhost:1234/rns?peer=1'),'ws://localhost:1234/rns?peer=1');assert.equal(normalizeTrackTransportEndpoint('ws://[::1]:1234/'),'ws://[::1]:1234/');
 for(const endpoint of ['ws://example.test/','http://localhost/','wss://user:password@example.test/','wss://example.test/#key','invalid','wss://example.test/'+ 'x'.repeat(4096)])assert.throws(()=>normalizeTrackTransportEndpoint(endpoint),error=>error.code==='TRACK_TRANSPORT_ENDPOINT_INVALID');
});
test('three durable network-session errors have complete copy in eleven languages',()=>{
 for(const {id} of locales)for(const code of ['TRACK_TRANSPORT_STORAGE','TRACK_TRANSPORT_CORRUPT','TRACK_TRANSPORT_FULL'])assert.ok(trackTransportText(id,code)?.trim());assert.equal(trackTransportText('zh-Hans','OTHER'),null);assert.throws(()=>trackTransportText('missing','TRACK_TRANSPORT_STORAGE'));
});
