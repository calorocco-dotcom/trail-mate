import test from 'node:test';import assert from 'node:assert/strict';
import {readProtobufMessage,protobufVarintField,protobufBytesField,patchSingularProtobufField} from '../../site/device/src/module-protobuf-wire.js';
const join=(...parts)=>Uint8Array.from(parts.flatMap(part=>[...part]));
test('editing nested MQTT flag preserves unknown fields and module version bytes',()=>{
 const unknown=new Uint8Array([0x90,3,0x81,0]),nestedUnknown=protobufBytesField(99,new Uint8Array([255,0,9]));
 const mqtt=join(protobufVarintField(1,1),nestedUnknown),version=protobufVarintField(8,4),source=join(unknown,protobufBytesField(1,mqtt),version),original=source.slice();
 const updated=patchSingularProtobufField(source,[1,1],protobufVarintField(1,0)),fields=readProtobufMessage(updated);
 assert.deepEqual(source,original);assert.deepEqual(fields[0].raw,unknown);assert.deepEqual(fields[2].raw,version);
 const children=readProtobufMessage(fields[1].value);assert.equal(children[0].value,0n);assert.deepEqual(children[1].raw,nestedUnknown);
});
test('duplicate singular messages merge instead of losing earlier fields',()=>{
 const enabled=protobufVarintField(1,1),username=protobufBytesField(3,new TextEncoder().encode('alice')),password=protobufBytesField(4,new TextEncoder().encode('secret'));
 const source=join(protobufBytesField(1,enabled),protobufBytesField(1,username)),updated=patchSingularProtobufField(source,[1,4],password),parents=readProtobufMessage(updated);
 assert.equal(parents.length,1);const fields=readProtobufMessage(parents[0].value);assert.deepEqual(fields.map(field=>field.number),[1,3,4]);assert.deepEqual(fields[0].raw,enabled);assert.deepEqual(fields[1].raw,username);
 const duplicate=join(protobufVarintField(1,1),protobufVarintField(2,7),protobufVarintField(1,0));const replaced=readProtobufMessage(patchSingularProtobufField(duplicate,[1],protobufVarintField(1,3)));assert.deepEqual(replaced.map(field=>field.number),[1,2]);assert.equal(replaced[0].value,3n);
});
test('absent nested paths can be created; deletion retains explicit parent presence',()=>{
 const value=patchSingularProtobufField(new Uint8Array(),[1,11,2],protobufVarintField(2,16));assert.equal(readProtobufMessage(readProtobufMessage(readProtobufMessage(value)[0].value)[0].value)[0].value,16n);
 const cleared=patchSingularProtobufField(value,[1,11,2],null);assert.equal(readProtobufMessage(readProtobufMessage(cleared)[0].value)[0].value.length,0);
});
test('unknown fixed fields and matched groups are preserved',()=>{
 const unknown=new Uint8Array([0x19,1,2,3,4,5,6,7,8,0x25,9,10,11,12,0x2b,0x08,1,0x2c]),updated=patchSingularProtobufField(unknown,[1],protobufVarintField(1,2));
 assert.deepEqual(updated.slice(0,unknown.length),unknown);assert.deepEqual(readProtobufMessage(updated).map(field=>field.wire),[1,5,3,0]);
});
test('malformed protobuf, unsafe integers, wrong paths and size overflow are rejected',()=>{
 for(const bytes of [[0],[8,128],[10,4,1],[11,20],[12],[14],[8,...new Array(10).fill(255)]])assert.throws(()=>readProtobufMessage(Uint8Array.from(bytes)));
 assert.equal(readProtobufMessage(protobufVarintField(1,0xffffffffffffffffn))[0].value,0xffffffffffffffffn);
 assert.throws(()=>protobufVarintField(1,Number.MAX_SAFE_INTEGER+1));assert.throws(()=>patchSingularProtobufField(new Uint8Array(),[1],protobufVarintField(2,1)));
 assert.throws(()=>patchSingularProtobufField(protobufVarintField(1,1),[1,2],protobufVarintField(2,1)));
 assert.throws(()=>patchSingularProtobufField(protobufBytesField(99,new Uint8Array(660)),[1],protobufBytesField(1,new Uint8Array(20))),error=>error.code==='MODULE_PROTOBUF_LIMIT');
});
