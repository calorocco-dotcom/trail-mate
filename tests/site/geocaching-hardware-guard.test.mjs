import test from 'node:test';import assert from 'node:assert/strict';
import {GeocachingHardwareExchange} from '../../tools/hostlink/geocaching_hardware_exchange.mjs';
import {encode,equal} from '../../site/geocaching/src/protocol.js';
import {encodeDraftEdit} from '../../site/geocaching/src/device-draft.js';
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

test('Signature continuation admits only the saved v2 edit over its confirmed v1 base',async()=>{
 const base=new Uint8Array(32).fill(7),observe=GeocachingHardwareExchange.prototype.observe;
 const current=[id,3,publicKey,'Wio网页验收-test-v2',0,310000000,1210000000,base,[1,1,4]];
 const make=()=>Object.assign(fixture(),{resumeBaseHash:base,resumeSignature:{draftGeneration:3n}});
 for(const changed of [
  [id,4,...current.slice(2)],
  [...current.slice(0,3),'Wio网页验收-test',...current.slice(4)],
  [...current.slice(0,7),new Uint8Array(32),current[8]],
  [...current.slice(0,8),[1,1,0]],
 ]){
  const device=make();await assert.rejects(observe.call(device,{code:64,body:[]},[1,0,1,changed]));
  assert.equal(device.drafts.size,0);
 }
 const device=make();await observe.call(device,{code:64,body:[]},[1,0,1,current]);assert.ok(device.drafts.has(idHex));
});

test('sealed draft reconnect retains only the exact planned edit of the read physical fixture',async()=>{
 const device=Object.assign(fixture(),{outputs:new Map(),transfers:new Map(),fingerprint:'expected'});
 device.author.fingerprint='expected';device.drafts.add(idHex);
 const fields={state:0,latitudeE7:310000000,longitudeE7:1210000000,name:device.namePrefix,
  description:'original body',hint:'hint',difficultyX2:2,terrainX2:2,containerSize:0};
 const stored=encode([publicKey,new Uint8Array(32),[0,310000000,1210000000,fields.name,fields.description,fields.hint,2,2,0],1]);
 const observe=GeocachingHardwareExchange.prototype.observe,guard=GeocachingHardwareExchange.prototype.guard;
 for(let offset=0;offset<stored.length;offset+=64)
  await observe.call(device,{code:65,body:[id,offset,64,1,2]},[id,1,stored.length,offset,stored.slice(offset,offset+64)]);
 const source=encodeDraftEdit(id,{...fields,name:device.namePrefix+'-v2'},1),hash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
 guard.call(device,{code:32,body:[id,2,source.length,hash]});
 await observe.call(device,{code:32,body:[id,2,source.length,hash]},[id,320,source.length,1]);
 assert.ok(device.transfers.get(idHex).sealed);assert.ok(equal(device.transfers.get(idHex).content,source));
 guard.call(device,{code:66,body:[id,id,1,2]});
 const other=new Uint8Array(16).fill(9),wrongHash=new Uint8Array(32).fill(7);
 guard.call(device,{code:32,body:[other,2,source.length,wrongHash]});
 await assert.rejects(observe.call(device,{code:32,body:[other,2,source.length,wrongHash]},[other,320,source.length,1]));
 assert.equal(device.transfers.get(Buffer.from(other).toString('hex')).sealed,false);
});

test('Signature continuation recognizes only its exact already-reserved input',async()=>{
 const raw=encode(['retained acceptance input']),base=new Uint8Array(32).fill(7),prefix=new TextEncoder().encode('trailmate.geocache/revision/v1\0');
 const input=new Uint8Array(prefix.length+raw.length);input.set(prefix);input.set(raw,prefix.length);
 const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',input));
 const make=()=>Object.assign(fixture(),{resumeBaseHash:base,resumeSignature:{draftGeneration:3n,raw}});
 const row=[id,4,publicKey,'Wio网页验收-test-v2',0,310000000,1210000000,hash,[1,1,4]],observe=GeocachingHardwareExchange.prototype.observe;
 for(const changed of [[id,5,...row.slice(2)],[...row.slice(0,7),base,row[8]]])await assert.rejects(observe.call(make(),{code:64,body:[]},[1,0,1,changed]));
 const device=make();await observe.call(device,{code:64,body:[]},[1,0,1,row]);assert.ok(device.drafts.has(idHex));
});
