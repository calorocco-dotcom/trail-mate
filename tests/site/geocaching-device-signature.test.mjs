import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {beginDeviceGeocacheSignature,readDeviceGeocacheSignature,uploadUnsignedDeviceCache} from '../../site/geocaching/src/device-signature.js';
import {DeviceDraftReader} from '../../site/geocaching/src/device-draft-reader.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {readCurrentDeviceAuthor} from '../../site/geocaching/src/current-device-author.js';
const join=(...parts)=>Uint8Array.from(parts.flatMap(part=>Array.from(part)));

test('Retiring an old signature preserves a replaced/terminal journal and rejects ambiguous failures',async()=>{
 const author={verified:true,publicKey:new Uint8Array(64).fill(1),generation:2},id=new Uint8Array(16).fill(2);
 for(const code of [4,10,2,5,6,'TIMEOUT']){
  const calls=[],reader=new DeviceDraftReader({async request(op,body){calls.push(op);if(op===3)return [];assert.equal(op,36);assert.deepEqual(body,[id]);throw Object.assign(Error('status'),{code});}});
  if(code===4||code===10)await reader.clearSignatureTransfer(id,author);
  else await assert.rejects(reader.clearSignatureTransfer(id,author),error=>error.code===code);
  assert.deepEqual(calls,[36,3]);await reader.close();
 }
});
test('Author Busy is bounded and keeps the original identity without repeating task admission',async()=>{
 const f=await fixture(),base=f.session.request;let busy=true;
 f.session.request=async(op,body)=>{if(op===1&&busy){busy=false;f.calls.push([op,body]);throw Object.assign(Error('Busy'),{code:5});}return base(op,body);};
 assert.equal((await readCurrentDeviceAuthor(f.session,f.author,{sleep:async()=>{}})).generation,7);
 assert.deepEqual(f.calls.map(call=>call[0]),[1,1]);
 let clock=0,calls=0;
 await assert.rejects(readCurrentDeviceAuthor({async request(){calls++;throw Object.assign(Error('Busy'),{code:5});}},f.author,{timeoutMs:250,retryMs:100,now:()=>clock,sleep:async ms=>{clock+=ms;}}),{code:5});assert.equal(calls,4);
 for(const code of [4,6,9,10,'TIMEOUT']){let count=0;await assert.rejects(readCurrentDeviceAuthor({async request(){count++;throw Object.assign(Error('failed'),{code});}},f.author),{code});assert.equal(count,1);}
 const controller=new AbortController();await assert.rejects(readCurrentDeviceAuthor({async request(){controller.abort(Error('stop'));throw Object.assign(Error('Busy'),{code:5});}},f.author,{signal:controller.signal}),/stop/);
});
test('reader holds one USB maintenance lease through upload, signing and verified result',async()=>{
 const f=await fixture(),base=f.session.request,id=new Uint8Array(16).fill(9);let offset=0;
 f.session.request=async(op,body)=>{
  if(op===32){f.calls.push([op,body]);return [id,320,0,0];}
  if(op===33){f.calls.push([op,body]);offset+=body[2].length;return [id,320,offset,0];}
  if(op===35){f.calls.push([op,body]);return [id,320,offset,1];}
  if(op===3){f.calls.push([op,body]);return [];}
  return base(op,body);
 };
 const reader=new DeviceDraftReader(f.session);
 const result=await reader.signRecord(f.source,f.author,{transferId:id,operationId:f.binding.operationId,operations:[32,33,35,71,72,73]});
 assert.equal(result.ready,true);assert.equal(f.calls.at(-1)[0],3);assert.equal(f.calls.filter(([op])=>op===3).length,1);
 assert.ok(f.calls.findIndex(([op])=>op===71)<f.calls.findIndex(([op])=>op===73));await reader.close();
});
test('unsigned cache uploads as type 5 with bounded chunks; old firmware gets no upload',async()=>{
 const f=await fixture(),base=f.session.request,id=new Uint8Array(16).fill(9),copied=new Uint8Array(f.source.length);let offset=0;
 f.session.request=async(op,body)=>{
  if(op===1)return base(op,body);
  f.calls.push([op,body]);
  if(op===32){assert.equal(body[1],5);assert.equal(body[2],f.source.length);return [id,320,0,0];}
  if(op===33){assert.equal(body[1],offset);assert.ok(body[2].length<=320);copied.set(body[2],offset);offset+=body[2].length;return [id,320,offset,0];}
  if(op===35)return [id,320,offset,1];throw Error('Unexpected upload command');
 };
 await assert.rejects(uploadUnsignedDeviceCache(f.session,f.source,f.author,{id,operations:[32,33,35]}));assert.equal(f.calls.length,0);
 const result=await uploadUnsignedDeviceCache(f.session,f.source,f.author,{id,operations:[32,33,35,71,72,73]});
 assert.equal(result.sealed,true);assert.deepEqual(copied,f.source);assert.deepEqual(result.sha256,f.binding.hash);
});
async function fixture(){
 const identity=await Identity.generate(),pub=await identity.getPublicKey(),utf8=new TextEncoder();
 const destination=await Identity.truncatedHash(join((await Identity.fullHash(utf8.encode('lxmf.delivery'))).slice(0,10),identity.identityHash));
 const source=encode([1,pub,new Uint8Array(16).fill(1),1,null,0,1,1,'Cache','a'.repeat(1024),'',2,2,0,1,1]);
 const signed=[source,await identity.sign(join(utf8.encode('trailmate.geocache/sign/v1\0'),source))],output=encode(signed);
 const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',source)),operationId=new Uint8Array(16).fill(2);
 const author={verified:true,publicKey:pub,generation:7},binding={operationId,hash,generation:7},calls=[];
 const session={async request(op,body){calls.push([op,body]);if(op===1)return [pub,identity.identityHash,destination,7];
  if(op===71)return [operationId,hash,7,0,0];if(op===72)return [operationId,hash,7,1,output.length];
  if(op===73)return [operationId,body[1],output.length,hash,output.slice(body[1],body[1]+320)];throw Error('Unexpected command');}};
 return {session,source,author,binding,calls,transfer:{sealed:true,id:new Uint8Array(16).fill(3),sha256:hash,bytes:source.length},options:{operationId,operations:[71,72,73]}};
}
test('starts once, then reads and verifies multi-chunk signed content without restarting',async()=>{
 const f=await fixture(),binding=await beginDeviceGeocacheSignature(f.session,f.transfer,f.author,f.options);
 const result=await readDeviceGeocacheSignature(f.session,binding,f.source,f.author);
 assert.equal(result.ready,true);assert.deepEqual(result.signed[0],f.source);
 assert.equal(f.calls.filter(([op])=>op===71).length,1);assert.ok(f.calls.filter(([op])=>op===73).length>3);
});
test('draft-bound signing freezes the ID and preserves the full uint64 generation',async()=>{
 const f=await fixture(),draftId=new Uint8Array(16).fill(4),generation=0xffffffffffffffffn;
 const task=beginDeviceGeocacheSignature(f.session,f.transfer,f.author,{...f.options,draftId,draftGeneration:generation});
 draftId.fill(8);await task;const body=f.calls.find(([op])=>op===71)[1];
 assert.equal(body.length,7);assert.deepEqual(body[5],new Uint8Array(16).fill(4));assert.equal(body[6],generation);
 for(const value of [0n,-1n,0x10000000000000000n]){
  const before=f.calls.length;await assert.rejects(beginDeviceGeocacheSignature(f.session,f.transfer,f.author,{...f.options,draftId,draftGeneration:value}));assert.equal(f.calls.length,before);
 }
});
test('draft-bound signing survives actual management encoding for small and full-width generations',async()=>{
 for(const generation of [1n,127n,128n,65535n,4294967296n,BigInt(Number.MAX_SAFE_INTEGER),BigInt(Number.MAX_SAFE_INTEGER)+1n,0xffffffffffffffffn]){
  const f=await fixture(),request=f.session.request;
  f.session.request=(operation,body)=>{const decoded=decodeManagement(encodeManagement(new Uint8Array(16).fill(1),new Uint8Array(16).fill(2),operation,body));return request(decoded.code,decoded.body);};
  await beginDeviceGeocacheSignature(f.session,f.transfer,f.author,{...f.options,draftId:new Uint8Array(16).fill(4),draftGeneration:generation});
  assert.equal(BigInt(f.calls.find(([op])=>op===71)[1][6]),generation);
 }
});
test('missing capability or changed author rejects before signing',async()=>{
 const f=await fixture();await assert.rejects(beginDeviceGeocacheSignature(f.session,f.transfer,f.author,{...f.options,operations:[]}));assert.equal(f.calls.length,0);
 f.author.generation=8;await assert.rejects(beginDeviceGeocacheSignature(f.session,f.transfer,f.author,f.options),e=>e.code==='STALE_IDENTITY');
 assert.equal(f.calls.filter(([op])=>op===71).length,0);
});
test('explicit storage Busy retries the same binding within budget',async()=>{
 const f=await fixture(),request=f.session.request;let busy=2,clock=0;
 f.session.request=async(op,body)=>{if(op===71&&busy-- >0){f.calls.push([op,body]);throw Object.assign(Error('Initializing'),{code:5});}return request(op,body);};
 const result=await beginDeviceGeocacheSignature(f.session,f.transfer,f.author,{...f.options,now:()=>clock,sleep:async ms=>{clock+=ms;}});
 assert.equal(result.phase,0);const requests=f.calls.filter(([op])=>op===71);assert.equal(requests.length,3);
 for(const [,body] of requests)assert.deepEqual(body,requests[0][1]);
});
test('lost signing reply retains operation binding and never retries; source mismatch never reads',async()=>{
 const f=await fixture(),request=f.session.request;
 f.session.request=async(op,body)=>{if(op===71){f.calls.push([op,body]);throw Error('Lost reply');}return request(op,body);};
 await assert.rejects(beginDeviceGeocacheSignature(f.session,f.transfer,f.author,f.options),e=>e.code==='SIGNATURE_UNCONFIRMED'&&e.binding.operationId[0]===2);
 assert.equal(f.calls.filter(([op])=>op===71).length,1);
 f.source[0]^=1;const count=f.calls.length;
 await assert.rejects(readDeviceGeocacheSignature(f.session,f.binding,f.source,f.author),/hash/);assert.equal(f.calls.length,count);
});

test('an admitted signature remains under one lease through Busy status and result reads',async()=>{
 const f=await fixture(),base=f.session.request,id=new Uint8Array(16).fill(9);let offset=0,statusBusy=true,chunkBusy=true;
 f.session.request=async(op,body)=>{
  if(op===32){f.calls.push([op,body]);return [id,320,0,0];}
  if(op===33){f.calls.push([op,body]);offset+=body[2].length;return [id,320,offset,0];}
  if(op===35){f.calls.push([op,body]);return [id,320,offset,1];}
  if(op===3){f.calls.push([op,body]);return [];}
  if(op===72&&statusBusy){statusBusy=false;f.calls.push([op,body]);throw Object.assign(Error('Runtime guard busy'),{code:5});}
  if(op===73&&body[1]===320&&chunkBusy){chunkBusy=false;f.calls.push([op,body]);throw Object.assign(Error('Runtime guard busy'),{code:5});}
  return base(op,body);
 };
 const reader=new DeviceDraftReader(f.session),result=await reader.signRecord(f.source,f.author,{transferId:id,operationId:f.binding.operationId,operations:[32,33,35,71,72,73]});
 assert.equal(result.ready,true);assert.deepEqual(result.signed[0],f.source);assert.equal(f.calls.filter(([op])=>op===71).length,1);assert.equal(f.calls.filter(([op])=>op===3).length,1);
 assert.equal(f.calls.filter(([op])=>op===72).length,3);for(const [op,body] of f.calls)if(op===72||op===73)assert.deepEqual(body[0],f.binding.operationId);await reader.close();
});

test('Busy has no invented signature phase, while other failures and cancellation are preserved',async()=>{
 for(const code of [3,4,6,9,10,'TIMEOUT']){
  const f=await fixture(),base=f.session.request;f.session.request=async(op,body)=>{if(op===72){f.calls.push([op,body]);throw Object.assign(Error('Rejected'),{code});}return base(op,body);};
  await assert.rejects(readDeviceGeocacheSignature(f.session,f.binding,f.source,f.author),error=>error.code===code);assert.equal(f.calls.filter(([op])=>op===72).length,1);
 }
 const f=await fixture(),base=f.session.request;f.session.request=async(op,body)=>{if(op===72)throw Object.assign(Error('Busy'),{code:5});return base(op,body);};
 const result=await readDeviceGeocacheSignature(f.session,f.binding,f.source,f.author);assert.equal(result.ready,false);assert.equal(result.busy,true);assert.equal(result.phase,undefined);assert.deepEqual(result.binding,f.binding);
 const controller=new AbortController();f.session.request=async(op,body)=>{if(op===72){controller.abort(new DOMException('Stopped','AbortError'));throw Object.assign(Error('Busy'),{code:5});}return base(op,body);};
 await assert.rejects(readDeviceGeocacheSignature(f.session,f.binding,f.source,f.author,{signal:controller.signal}),error=>error.name==='AbortError');
});
