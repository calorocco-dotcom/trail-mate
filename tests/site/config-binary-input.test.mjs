import test from 'node:test';import assert from 'node:assert/strict';
import {decodeConfigurationBinary} from '../../site/device/src/config-binary-input.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
test('hexadecimal and Base64 group hashes produce identical persisted bytes',()=>{
 const key='rt.group.0.identity_hash',hex='00112233445566778899aabbccddeeff',bytes=Uint8Array.from(hex.match(/../g),pair=>parseInt(pair,16)),base64=btoa(String.fromCharCode(...bytes));
 for(const input of [hex,hex.toUpperCase(),'0x'+hex,' '+hex+' ',base64]){const actual=decodeConfigurationBinary(key,input);assert.deepEqual(actual,bytes);const document=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\n'+key+'=b64:'+base64+'\nEND\n'));document.set(key,actual);assert.deepEqual(new TmsDocument(document.serialize()).get(key).value,bytes);}
});
test('other binary settings retain their unambiguous Base64 interpretation',()=>{
 const input='a'.repeat(32);assert.equal(decodeConfigurationBinary('mt.primary_psk',input).length,24);assert.throws(()=>decodeConfigurationBinary('rt.group.0.identity_hash','invalid!'));
});
