import test from 'node:test';import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {saveConfigDocument} from '../../site/device/src/config-save.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {validateKnownModuleDocument} from '../../site/device/src/config-module-validation.js';
import {replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
import {protobufBytesField} from '../../site/device/src/module-protobuf-wire.js';
const fixture=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Original\nmt_module.format=str:pb-v1\nmt_module.size=u16:2\nmt_module.chunk_count=u8:3\nmt_module.chunk.0=b64:QAE=\nmt_module.chunk.1=b64:\nmt_module.chunk.2=b64:\nEND\n'));
test('malformed module container and protobuf never reach a serial request',async()=>{
 const broken=fixture();broken.set('mt_module.size',3);
 const oversized=replaceMeshtasticModule(fixture(),protobufBytesField(1,protobufBytesField(4,new Uint8Array(32).fill(65))));
 for(const document of [broken,oversized]){
  let calls=0;await assert.rejects(saveConfigDocument({async request(){calls++;}},document,new Uint8Array(32),{id:new Uint8Array(16)}),error=>error.code?.startsWith('MODULE_'));
  assert.equal(calls,0);
 }
});
test('invalid module backup rolls back all candidate edits',()=>{
 const current=fixture(),before=current.serialize(),backup=new TmsDocument(current.serialize({kind:'backup'}));
 backup.set('device.node_name','FromBackup');backup.set('mt_module.size',3);
 const result=prepareConfigImport(current,backup);assert.equal(result.changed.length,0);assert.ok(result.errors.some(error=>error.key==='mt_module'));
 assert.deepEqual(result.document.serialize(),before);assert.deepEqual(current.serialize(),before);
});
test('absent and future module formats are preserved as opaque while known format is validated',()=>{
 validateKnownModuleDocument(fixture());const future=fixture();future.set('mt_module.format','pb-v2');future.set('mt_module.size',3);
 const before=future.serialize();validateKnownModuleDocument(future);assert.deepEqual(future.serialize(),before);
 validateKnownModuleDocument(new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nEND\n')));
});
