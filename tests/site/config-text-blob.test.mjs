import test from 'node:test';import assert from 'node:assert/strict';
import {configurationTextBlobValue,encodeConfigurationTextBlob} from '../../site/device/src/config-text-blob.js';
import {validateConfigurationField} from '../../site/device/src/config-schema.js';
const record=value=>({key:'chat.auto_reply.text',type:'b64',value});
test('automatic reply text round trips Unicode, whitespace, line breaks and BOM',()=>{
 for(const text of ['', 'Hello\n世界 🌍', '  حافظ على النص  ', '\ufeffreply']){const value=encodeConfigurationTextBlob(text);validateConfigurationField(record(value),value);assert.equal(configurationTextBlobValue(record(value)),text);assert.deepEqual(value,new TextEncoder().encode(text));}
});
test('device capacity uses UTF-8 bytes and rejects malformed or truncated text',()=>{
 assert.equal(encodeConfigurationTextBlob('界'.repeat(57)).length,171);
 assert.throws(()=>encodeConfigurationTextBlob('界'.repeat(58)),error=>error.code==='CONFIG_UTF8_LIMIT'&&error.params.max===171);
 for(const text of ['before\0after','\ud800'])assert.throws(()=>encodeConfigurationTextBlob(text),error=>error.code==='invalid');
 for(const value of [new Uint8Array([0xc0,0xaf]),new Uint8Array([0xe2,0x82]),new Uint8Array([0])]){assert.equal(configurationTextBlobValue(record(value)),undefined);assert.throws(()=>validateConfigurationField(record(value),value),error=>error.code==='invalid');}
});
test('unrelated binary fields remain binary and invalid legacy bytes are preserved',()=>{
 const bytes=new Uint8Array([255,0,4]),before=bytes.slice();assert.equal(configurationTextBlobValue(record(bytes)),undefined);assert.deepEqual(bytes,before);
 assert.equal(configurationTextBlobValue({key:'rt.group.0.identity_hash',type:'b64',value:new Uint8Array(16).fill(1)}),undefined);
});
