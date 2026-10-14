import test from 'node:test';import assert from 'node:assert/strict';
import {GeocachingHardwareExchange} from '../../tools/hostlink/geocaching_hardware_exchange.mjs';
const id=new Uint8Array(16).fill(3),publicKey=new Uint8Array(64).fill(4),idHex=Buffer.from(id).toString('hex');
function fixture(){return {drafts:new Set(),resumeDraftId:idHex,namePrefix:'Wio网页验收-test',author:{publicKey}};}
const row=[id,1,null,'Wio网页验收-test',0,310000000,1210000000,null,[0,0,0]];
test('Hardware resume identifies only the exact isolated unpublished fixture',async()=>{
 const device=fixture(),observe=GeocachingHardwareExchange.prototype.observe;
 for(const changed of [[...row.slice(0,3),'User cache',...row.slice(4)],[...row.slice(0,4),2,...row.slice(5)],[...row.slice(0,8),[1,1,0]]]){
  await assert.rejects(observe.call(device,{code:64,body:[]},[1,0,1,changed]));assert.equal(device.drafts.size,0);
 }
 await observe.call(device,{code:64,body:[]},[1,0,1,row]);assert.ok(device.drafts.has(idHex));
 const changed=[id,5,publicKey,'Wio网页验收-test-v2',2,310000000,1210000000,null,[3,3,0]];
 await observe.call(device,{code:64,body:[]},[5,0,1,changed]);assert.equal(device.drafts.size,1);
});
test('Hardware signature read permits an identical prefix retry while binding total/hash/bytes',async()=>{
 const device={outputs:new Map(),completed:new Set(),signedVersions:[]},observe=GeocachingHardwareExchange.prototype.observe;
 const hash=new Uint8Array(32).fill(6),chunk=new Uint8Array([1,2]),reply=[id,0,8,hash,chunk];
 await observe.call(device,{code:73,body:[id,0,320]},reply);
 await observe.call(device,{code:73,body:[id,0,320]},reply);
 assert.equal(device.outputs.get(idHex).offset,2);
 await assert.rejects(observe.call(device,{code:73,body:[id,0,320]},[id,0,8,hash,new Uint8Array([1,3])]));
 await assert.rejects(observe.call(device,{code:73,body:[id,0,320]},[id,0,8,new Uint8Array(32),chunk]));
 await assert.rejects(observe.call(device,{code:73,body:[id,0,320]},[id,0,9,hash,chunk]));
 assert.equal(device.outputs.get(idHex).offset,2);
});
