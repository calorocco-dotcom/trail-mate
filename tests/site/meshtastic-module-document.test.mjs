import test from 'node:test';import assert from 'node:assert/strict';import {readFile} from 'node:fs/promises';
import {TmsDocument} from '../../site/device/src/tms-document.js';import {MODULE_LIMIT,readMeshtasticModule,replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Before\nfuture.vendor=opaque:keep-exactly\nmt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:CAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\nEND\n'));
test('module bounds match firmware generated size and fixed chunk format',async()=>{
 const header=await readFile(new URL('../../modules/core_chat/generated/meshtastic/localonly.pb.h',import.meta.url),'utf8'),extension=await readFile(new URL('../../platform/esp/arduino_common/src/app_config_tms_ble_extension.cpp',import.meta.url),'utf8');
 assert.equal(Number(header.match(/#define meshtastic_LocalModuleConfig_size\s+(\d+)/)[1]),MODULE_LIMIT.bytes);assert.match(extension,/kModuleChunkBytes = 240U/);assert.match(extension,/kModuleChunkCount == 3U/);
});
test('module updates span all chunk boundaries without mutating source or unrelated edits',()=>{
 for(const length of [1,239,240,241,479,480,481,673]){const source=fixture();source.set('device.node_name','Edited');const original=source.serialize(),bytes=Uint8Array.from({length},(_,index)=>index%256),updated=replaceMeshtasticModule(source,bytes);
  assert.deepEqual(source.serialize(),original);assert.deepEqual(readMeshtasticModule(updated),bytes);assert.equal(updated.get('device.node_name').value,'Edited');assert.equal(updated.get('future.vendor').raw,'keep-exactly');assert.equal(updated.get('mt_module.chunk_count').value,3);
  const roundTrip=new TmsDocument(updated.serialize());assert.deepEqual(readMeshtasticModule(roundTrip),bytes);bytes.fill(0);assert.ok(readMeshtasticModule(updated).some(Boolean)||length===1);
 }
});
test('shrinking clears trailing chunks and invalid input leaves the document intact',()=>{
 const expanded=replaceMeshtasticModule(fixture(),new Uint8Array(600).fill(9)),updated=replaceMeshtasticModule(expanded,new Uint8Array([8,2]));assert.equal(updated.get('mt_module.chunk.1').value.length,0);assert.equal(updated.get('mt_module.chunk.2').value.length,0);
 const original=expanded.serialize();for(const value of [new Uint8Array(),new Uint8Array(674),'wrong'])assert.throws(()=>replaceMeshtasticModule(expanded,value),error=>error.code==='MODULE_SIZE_INVALID');assert.deepEqual(expanded.serialize(),original);
 expanded.set('mt_module.chunk.2',new Uint8Array(1));const broken=expanded.serialize();assert.throws(()=>replaceMeshtasticModule(expanded,new Uint8Array([8,1])),error=>error.code==='MODULE_CHUNK_INVALID');assert.deepEqual(expanded.serialize(),broken);
});
