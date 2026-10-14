import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {confirmDeviceTrackPublication} from '../../site/tracks/src/device-ip-confirmation.js';
import {encodeManagement} from '../../site/device/src/management-protocol.js';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
const vector=JSON.parse(execFileSync(python,['-m','tools.tracks.receipt_vector'],{encoding:'utf8'}));
const bin=hex=>new Uint8Array(Buffer.from(hex,'hex'));
const manifest=vector.manifest.map(bin),fields=decode(manifest[0]),receipt=vector.receipt.map(bin),receiptFields=decode(receipt[0]);
const key=fields[1],directory=bin(vector.directory),id=new Uint8Array(16).fill(22),operationId=new Uint8Array(16).fill(23);
const identity=await Identity.fromPublicKey(key),nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
const delivery=await Identity.truncatedHash(new Uint8Array([...nameHash,...identity.identityHash]));
const author={verified:true,publicKey:key,generation:7};
const record={stage:'confirmed',draftId:'confirmation',draftVersion:1,authorKey:key,requestId:bin(vector.request),directoryKey:directory,
 delivery:receiptFields[9],epoch:bin(vector.epoch),signature:manifest[1],receipt:encode(receipt),
 plan:{operationId,kmlUploadId:new Uint8Array(16).fill(24),manifestUploadId:new Uint8Array(16).fill(25),generation:7,
  manifest:manifest[0],fileHash:fields[13][0],fileBytes:fields[13][1]}};
function session(mode='good'){
 const calls=[];let total=0,offset=0,attempts=0;
 return {calls,async request(op,body){calls.push({op,body});
  if(op===1)return[key,identity.identityHash,delivery,mode==='identity'?8:7];
  if(op===32){assert.equal(body[1],7);total=body[2];return[id,320,0,0];}
  if(op===33){assert.equal(body[1],offset);offset+=body[2].length;return[id,320,offset,0];}
  if(op===35){assert.equal(offset,total);return[id,320,total,1];}
  assert.equal(op,97);assert.equal(body.length,11);assert.deepEqual(body[0],operationId);assert.deepEqual(body[7],record.requestId);
  if(mode==='lost')throw Error('USB reply lost');
  if(mode==='busy'&&attempts++===0)throw Object.assign(Error('Busy'),{code:5});
  if(mode==='good'&&attempts++===0)return[operationId,id,7,0,null,null,0,0];
  return[operationId,id,7,1,receiptFields[2],mode==='hash'?new Uint8Array(32):receiptFields[3],receiptFields[4],mode==='durable'?0:1];
 }};
}
const options={id,operations:[32,33,35,97],sleep:async()=>{},now:()=>0};
test('confirmed directory receipt uploads once and verifies exact durable USB confirmation',async()=>{
 const device=session();const result=await confirmDeviceTrackPublication(device,record,author,options);
 assert.equal(result.confirmed,true);assert.equal(result.durable,true);
 assert.ok(device.calls.every(call=>[1,32,33,35,97].includes(call.op)));
 const chunks=device.calls.filter(call=>call.op===33).flatMap(call=>Array.from(call.body[2]));
 assert.deepEqual(new Uint8Array(chunks),record.receipt);
 const requests=device.calls.filter(call=>call.op===97);assert.equal(requests.length,2);assert.deepEqual(requests[0].body,requests[1].body);
});
test('lost confirmation reply retains binding and does not silently repeat writes',async()=>{
 const device=session('lost');await assert.rejects(confirmDeviceTrackPublication(device,record,author,options),error=>
  error.code==='TRACK_CONFIRMATION_UNCONFIRMED'&&assert.deepEqual(error.binding.id,id)===undefined);
 assert.equal(device.calls.filter(call=>call.op===97).length,1);
});
test('only explicit Busy retries; wrong durable marker/hash is not accepted',async()=>{
 const busy=session('busy');assert.equal((await confirmDeviceTrackPublication(busy,record,author,options)).confirmed,true);
 for(const mode of ['durable','hash'])await assert.rejects(confirmDeviceTrackPublication(session(mode),record,author,options),
  error=>error.code==='TRACK_CONFIRMATION_UNCONFIRMED');
});
test('unsupported capability and changed identity cause no upload',async()=>{
 const unsupported=session();await assert.rejects(confirmDeviceTrackPublication(unsupported,record,author,{...options,operations:[32,33,35]}));
 assert.equal(unsupported.calls.length,0);
 const changed=session('identity');await assert.rejects(confirmDeviceTrackPublication(changed,record,author,options),error=>error.code==='STALE_IDENTITY');
 assert.deepEqual(changed.calls.map(call=>call.op),[1]);
});
test('same author on a fresh device imports verified signed history with new generation and fixed upload IDs',async()=>{
 const manifestId=new Uint8Array(16).fill(26),calls=[],uploads=new Map();let activeId;
 const device={async request(op,body){calls.push({op,body});
  if(op===1)return[key,identity.identityHash,delivery,8];
  if(op===32){activeId=Buffer.from(body[0]).toString('hex');uploads.set(activeId,{kind:body[1],total:body[2],bytes:[]});return[body[0],320,0,0];}
  if(op===33){const entry=uploads.get(activeId);assert.equal(body[1],entry.bytes.length);entry.bytes.push(...body[2]);return[body[0],320,entry.bytes.length,0];}
  if(op===35){const entry=uploads.get(activeId);assert.equal(entry.bytes.length,entry.total);return[body[0],320,entry.total,1];}
  assert.equal(op,98);assert.equal(body.length,14);assert.equal(body[3],8);assert.deepEqual(body[11],manifestId);assert.deepEqual(body[13],record.signature);
  assert.ok(encodeManagement(id,operationId,98,body).length<=512);
  return[body[0],id,8,1,receiptFields[2],receiptFields[3],receiptFields[4],1];
 }};
 const result=await confirmDeviceTrackPublication(device,record,{...author,generation:8},{...options,operations:[32,33,35,98],manifestId,importHistory:true});
 assert.equal(result.durable,true);assert.equal(result.authorGeneration,8);assert.deepEqual(result.manifestId,manifestId);
 const entries=[...uploads.values()];assert.deepEqual(entries.map(item=>item.kind),[4,7]);
 assert.deepEqual(new Uint8Array(entries[0].bytes),record.plan.manifest);assert.deepEqual(new Uint8Array(entries[1].bytes),record.receipt);
 assert.ok(calls.every(call=>[1,32,33,35,98].includes(call.op)));
});
test('history import requires explicit capability and stable distinct transfer IDs before any device call',async()=>{
 const device=session();
 for(const args of [{importHistory:true},{importHistory:true,manifestId:id},{importHistory:true,manifestId:new Uint8Array(16)}])
  await assert.rejects(confirmDeviceTrackPublication(device,record,author,{...options,operations:[32,33,35,98],...args}));
 assert.equal(device.calls.length,0);
});
