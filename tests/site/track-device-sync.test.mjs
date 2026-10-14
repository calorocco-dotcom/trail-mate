import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {saveTrackIPCheckpoint} from '../../site/tracks/src/ip-publication-journal.js';
import {syncTrackIPPublication,readTrackDeviceSyncState} from '../../site/tracks/src/device-sync-workflow.js';
import {trackDeviceSyncText} from '../../site/i18n/track-device-sync-labels.js';
import {locales} from '../../site/i18n/registry.js';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
const vector=JSON.parse(execFileSync(python,['-m','tools.tracks.receipt_vector'],{encoding:'utf8'})),bin=hex=>new Uint8Array(Buffer.from(hex,'hex'));
const signed=vector.manifest.map(bin),fields=decode(signed[0]),proof=vector.receipt.map(bin),receiptFields=decode(proof[0]);
const key=fields[1],identity=await Identity.fromPublicKey(key),nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
const delivery=await Identity.truncatedHash(new Uint8Array([...nameHash,...identity.identityHash])),author={verified:true,publicKey:key,generation:7};
const record={stage:'confirmed',draftId:'sync',draftVersion:1,authorKey:key,requestId:bin(vector.request),directoryKey:bin(vector.directory),
 delivery:receiptFields[9],epoch:bin(vector.epoch),signature:signed[1],receipt:encode(proof),
 plan:{operationId:new Uint8Array(16).fill(23),kmlUploadId:new Uint8Array(16).fill(24),manifestUploadId:new Uint8Array(16).fill(25),generation:7,
  manifest:signed[0],fileHash:fields[13][0],fileBytes:fields[13][1]}};
function fixture(){
 const entries=new Map(),storage={getItem:key=>entries.get(key)??null,setItem:(key,value)=>entries.set(key,value)};
 const options={storage,operations:[32,33,35,37,97],locks:{request:async(_name,_options,work)=>work({})},randomBytes:()=>new Uint8Array(16).fill(22),sleep:async()=>{},now:()=>0};
 saveTrackIPCheckpoint(record,{storage});let offset=0,total=0,transfer,kind;const calls=[];
 const session={calls,loseConfirmation:false,failCleanup:false,async request(op,body){calls.push({op,body});
  if(op===1)return[key,identity.identityHash,delivery,7];
  if(op===32){[transfer,kind,total]=body;offset=kind===7?0:total;return[transfer,320,offset,kind===7?0:1];}
  if(op===33){offset+=body[2].length;return[transfer,320,offset,0];}
  if(op===35)return[transfer,320,total,1];
  if(op===97){if(this.loseConfirmation){this.loseConfirmation=false;throw Error('Lost reply');}
   return[body[0],body[1],7,1,receiptFields[2],receiptFields[3],receiptFields[4],1];}
  if(op===37){assert.equal(body.length,4);if(this.failCleanup){this.failCleanup=false;throw Error('Cleanup interrupted');}return[];}
  throw Error('Unexpected operation '+op);
 }};
 return {storage,entries,session,options};
}
test('persist fixed receipt transfer before USB; retain durable acknowledgement before deleting exact uploads',async()=>{
 const f=fixture();let first=true;const device={request:async(op,body)=>{if(first){first=false;const state=await readTrackDeviceSyncState(record,f.options);assert.equal(state.stage,0);assert.ok(state.transferId.some(Boolean));}
  if(op===37)assert.equal((await readTrackDeviceSyncState(record,f.options)).stage,1);return f.session.request(op,body);}};
 const result=await syncTrackIPPublication(device,record,author,f.options);assert.equal(result.stage,2);assert.equal(result.cleaned,7);
 assert.deepEqual(f.session.calls.filter(call=>call.op===37).map(call=>call.body[0]),[record.plan.kmlUploadId,record.plan.manifestUploadId,new Uint8Array(16).fill(22)]);
 const count=f.session.calls.length;await syncTrackIPPublication(device,record,author,f.options);assert.equal(f.session.calls.length,count);
});
test('lost confirmation resumes same transfer and receipt without signing or publishing again',async()=>{
 const f=fixture();f.session.loseConfirmation=true;await assert.rejects(syncTrackIPPublication(f.session,record,author,f.options));
 assert.equal((await readTrackDeviceSyncState(record,f.options)).stage,0);assert.equal(f.session.calls.filter(call=>call.op===37).length,0);
 await syncTrackIPPublication(f.session,record,author,f.options);
 const requests=f.session.calls.filter(call=>call.op===97);assert.equal(requests.length,2);assert.deepEqual(requests[0].body,requests[1].body);
 assert.ok(f.session.calls.every(call=>[1,32,33,35,37,97].includes(call.op)));
});
test('cleanup failure resumes from durable checkpoint and never repeats confirmation',async()=>{
 const f=fixture();f.session.failCleanup=true;await assert.rejects(syncTrackIPPublication(f.session,record,author,f.options));
 assert.equal((await readTrackDeviceSyncState(record,f.options)).stage,1);
 await syncTrackIPPublication(f.session,record,author,f.options);assert.equal(f.session.calls.filter(call=>call.op===97).length,1);
 assert.equal((await readTrackDeviceSyncState(record,f.options)).stage,2);
});
test('storage failure before first USB write, changed identity and stale publication cannot mutate device',async()=>{
 const f=fixture();const failed={...f.options,storage:{getItem:f.storage.getItem,setItem:()=>{throw Error('Quota');}}};
 await assert.rejects(syncTrackIPPublication(f.session,record,author,failed));assert.equal(f.session.calls.length,0);
 await assert.rejects(syncTrackIPPublication(f.session,record,{...author,generation:8},f.options));assert.equal(f.session.calls.length,0);
 await assert.rejects(syncTrackIPPublication(f.session,{...record,requestId:new Uint8Array(16).fill(99)},author,f.options));assert.equal(f.session.calls.length,0);
});
test('failed browser acknowledgement write after device commit preserves sources for idempotent recovery',async()=>{
 const f=fixture();const options={...f.options,storage:{getItem:f.storage.getItem,setItem:(key,value)=>{
  if(key.startsWith('trailmate.track.device-sync.v1.')&&decode(bin(value))[3]===1)throw Error('Storage full after device commit');
  f.storage.setItem(key,value);
 }}};
 await assert.rejects(syncTrackIPPublication(f.session,record,author,options));
 assert.equal(f.session.calls.filter(call=>call.op===97).length,1);assert.equal(f.session.calls.filter(call=>call.op===37).length,0);
 assert.equal((await readTrackDeviceSyncState(record,f.options)).stage,0);
 await syncTrackIPPublication(f.session,record,author,f.options);assert.equal((await readTrackDeviceSyncState(record,f.options)).stage,2);
});
test('interrupted cleanup skips already acknowledged files on resume',async()=>{
 const f=fixture();let removed=0,fail=true;const device={request:async(op,body)=>{
  if(op===37&&++removed===2&&fail){fail=false;throw Error('Second file interrupted');}return f.session.request(op,body);
 }};
 await assert.rejects(syncTrackIPPublication(device,record,author,f.options));
 assert.equal((await readTrackDeviceSyncState(record,f.options)).cleaned,1);
 await syncTrackIPPublication(device,record,author,f.options);
 assert.equal(f.session.calls.filter(call=>call.op===37&&call.body[0][0]===24).length,1);
 assert.equal(f.session.calls.filter(call=>call.op===97).length,1);
 assert.equal((await readTrackDeviceSyncState(record,f.options)).stage,2);
});
test('retained synchronization is bound to complete signed checkpoint and all 11 languages exist',async()=>{
 const f=fixture();await syncTrackIPPublication(f.session,record,author,f.options);
 await assert.rejects(readTrackDeviceSyncState({...record,requestId:new Uint8Array(16).fill(99)},f.options));
 for(const locale of locales){const text=trackDeviceSyncText(locale.id);assert.equal(Object.keys(text).length,10);assert.ok(Object.values(text).every(value=>typeof value==='string'&&value.length));}
});
