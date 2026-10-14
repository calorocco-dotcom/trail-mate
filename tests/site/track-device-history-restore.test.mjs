import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {saveTrackIPCheckpoint} from '../../site/tracks/src/ip-publication-journal.js';
import {restoreTrackDeviceHistory,readTrackDeviceHistoryState} from '../../site/tracks/src/device-history-restore.js';
import {trackDeviceRestoreText} from '../../site/i18n/track-device-restore-labels.js';
import {locales} from '../../site/i18n/registry.js';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
const vector=JSON.parse(execFileSync(python,['-m','tools.tracks.receipt_vector'],{encoding:'utf8'})),bin=value=>new Uint8Array(Buffer.from(value,'hex'));
const signed=vector.manifest.map(bin),fields=decode(signed[0]),proof=vector.receipt.map(bin),receiptFields=decode(proof[0]),key=fields[1];
const identity=await Identity.fromPublicKey(key),nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),
 delivery=await Identity.truncatedHash(new Uint8Array([...nameHash,...identity.identityHash])),author={verified:true,publicKey:key,generation:8};
const record={stage:'confirmed',draftId:'restore-history',draftVersion:1,authorKey:key,requestId:bin(vector.request),directoryKey:bin(vector.directory),
 delivery:receiptFields[9],epoch:bin(vector.epoch),signature:signed[1],receipt:encode(proof),
 plan:{operationId:new Uint8Array(16).fill(23),kmlUploadId:new Uint8Array(16).fill(24),manifestUploadId:new Uint8Array(16).fill(25),generation:7,
 manifest:signed[0],fileHash:fields[13][0],fileBytes:fields[13][1]}};
function fixture(){
 const entries=new Map(),storage={getItem:key=>entries.get(key)??null,setItem:(key,value)=>entries.set(key,value)},calls=[];
 let serial=40,offset=0,total=0,transfer,kind;
 const options={storage,operations:[32,33,35,37,98],locks:{request:async(_name,_options,work)=>work({})},randomBytes:()=>new Uint8Array(16).fill(++serial),sleep:async()=>{},now:()=>0};
 saveTrackIPCheckpoint(record,{storage});
 const session={lose:false,failCleanup:false,async request(op,body){calls.push({op,body});
  if(op===1)return[key,identity.identityHash,delivery,8];
  if(op===32){assert.ok([...entries.keys()].some(key=>key.startsWith('trailmate.track.device-history.v1.')));[transfer,kind,total]=body;offset=0;return[transfer,320,0,0];}
  if(op===33){assert.equal(body[1],offset);offset+=body[2].length;return[transfer,320,offset,0];}
  if(op===35){assert.equal(offset,total);return[transfer,320,total,1];}
  if(op===98){assert.equal(body.length,14);assert.equal(body[3],8);if(this.lose){this.lose=false;throw Error('Lost import reply');}return[body[0],body[1],8,1,receiptFields[2],receiptFields[3],receiptFields[4],1];}
  if(op===37){if(this.failCleanup){this.failCleanup=false;throw Error('Lost cleanup reply');}assert.ok([4,7].includes(body[1]));assert.ok(![24,25].includes(body[0][0]));return[];}
  throw Error('Unexpected operation');
 }};
 return{storage,entries,calls,session,options};
}
test('replacement identity generation restores with persistent IDs, then cleans only the two new uploads',async()=>{
 const f=fixture(),done=await restoreTrackDeviceHistory(f.session,record,author,f.options);
 assert.equal(done.stage,2);assert.equal(done.cleaned,3);
 assert.deepEqual(f.calls.filter(call=>call.op===32).map(call=>call.body[1]),[4,7]);
 assert.deepEqual(f.calls.filter(call=>call.op===37).map(call=>call.body[0][0]),[41,42]);
 assert.equal((await readTrackDeviceHistoryState(record,{...f.options,generation:8})).stage,2);
});
test('lost confirmation and browser storage failure preserve fixed IDs and prohibit early cleanup',async()=>{
 const f=fixture();f.session.lose=true;
 await assert.rejects(restoreTrackDeviceHistory(f.session,record,author,f.options));
 const saved=await readTrackDeviceHistoryState(record,{...f.options,generation:8});assert.equal(saved.stage,0);assert.equal(f.calls.filter(call=>call.op===37).length,0);
 await restoreTrackDeviceHistory(f.session,record,author,f.options);
 const requests=f.calls.filter(call=>call.op===98);assert.deepEqual(requests[0].body,requests[1].body);
 const g=fixture(),options={...g.options,storage:{getItem:g.storage.getItem,setItem:(key,value)=>{
  if(key.startsWith('trailmate.track.device-history.v1.')&&decode(bin(value))[5]===1)throw Error('Storage full');g.storage.setItem(key,value);
 }}};
 await assert.rejects(restoreTrackDeviceHistory(g.session,record,author,options));assert.equal(g.calls.filter(call=>call.op===37).length,0);
});
test('completed or cleanup-pending browser state revalidates the current device rather than trusting an earlier device',async()=>{
 const f=fixture();f.session.failCleanup=true;await assert.rejects(restoreTrackDeviceHistory(f.session,record,author,f.options));
 assert.equal((await readTrackDeviceHistoryState(record,{...f.options,generation:8})).stage,1);
 await restoreTrackDeviceHistory(f.session,record,author,f.options);
 await restoreTrackDeviceHistory(f.session,record,author,f.options);
 assert.equal(f.calls.filter(call=>call.op===98).length,3);
 assert.deepEqual(f.calls.filter(call=>call.op===32).map(call=>call.body[0][0]),[41,42,41,42,41,42]);
});
test('different account, unsupported firmware, failed checkpoint persistence and changed history perform no USB writes',async()=>{
 const f=fixture();
 await assert.rejects(restoreTrackDeviceHistory(f.session,record,{...author,publicKey:new Uint8Array(64)},f.options));
 await assert.rejects(restoreTrackDeviceHistory(f.session,record,author,{...f.options,operations:[32,33,35,37,97]}));
 await assert.rejects(restoreTrackDeviceHistory(f.session,record,author,{...f.options,storage:{getItem:f.storage.getItem,setItem:()=>{throw Error('Storage full');}}}));
 await assert.rejects(restoreTrackDeviceHistory(f.session,{...record,requestId:new Uint8Array(16).fill(99)},author,f.options));
 assert.equal(f.calls.length,0);
 for(const locale of locales){const text=trackDeviceRestoreText(locale.id);assert.ok(text.restore&&text.intro);}
});
