import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {LXMessage} from '../../site/node_modules/@reticulum/lxmf/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {createTrackDirectoryReadSession,TRACK_DIRECTORY_APP} from '../../site/tracks/src/directory-read-session.js';

const directoryIdentity=await Identity.generate(),publicKey=await directoryIdentity.getPublicKey();
const name=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
const input=new Uint8Array(26);input.set(name);input.set(directoryIdentity.identityHash,10);
const delivery=await Identity.truncatedHash(input),local=new Uint8Array(16).fill(7),epoch=new Uint8Array(16).fill(3);
const caps=[1,TRACK_DIRECTORY_APP,publicKey,epoch,8388608,1024,32,[0,1,2,3,4,5,6,7,8]];
async function reply(call,{change,app=TRACK_DIRECTORY_APP,source=delivery,destination=local,tamper=false}={}){
 const request=decode(call.fields.get(0xfc));assert.equal(call.fields.get(0xfb),TRACK_DIRECTORY_APP);assert.deepEqual(call.directoryDelivery,delivery);
 const response=[1,1,request[2],request[3],200,request[2]===0?caps:[request[2]]];change?.(response);
 const message=new LXMessage({sourceHash:source,destinationHash:destination,title:'Track response',content:'',fields:new Map([[0xfb,app],[0xfc,encode(response)]])});
 const {wireData}=await message.serialize(directoryIdentity);if(tamper)wireData[32]^=1;return wireData;
}
const options=exchange=>({directoryPublicKey:publicKey,directoryDelivery:delivery,localDelivery:local,exchange});

test('real LXMF signature and capabilities pin the directory and unique request IDs',async()=>{
 const calls=[],session=await createTrackDirectoryReadSession(options(async call=>{calls.push(call);return reply(call);}));
 assert.equal(session.capabilities.chunkBytes,1024);assert.deepEqual(await session.request(2,[new Uint8Array(32),null]),[2]);assert.deepEqual(await session.request(8,[new Uint8Array(32),0,1]),[8]);
 const ids=calls.map(call=>Buffer.from(decode(call.fields.get(0xfc))[3]).toString('hex'));assert.equal(new Set(ids).size,3);
 session.close();await assert.rejects(session.request(2,[]),error=>error.code==='TRACK_DIRECTORY_CLOSED');
});
test('wrong source, destination, app, operation, request and invalid signatures are rejected',async()=>{
 for(const overrides of [{source:new Uint8Array(16)},{destination:new Uint8Array(16)},{app:'trailmate.geocache'},{change:row=>row[2]++},{change:row=>row[3]=new Uint8Array(16)},{tamper:true}])await assert.rejects(createTrackDirectoryReadSession(options(call=>reply(call,overrides))));
 await assert.rejects(createTrackDirectoryReadSession({...options(reply),directoryDelivery:new Uint8Array(16)}),error=>error.code==='TRACK_DIRECTORY_KEY_MISMATCH');
 await assert.rejects(createTrackDirectoryReadSession(options(call=>reply(call,{change:row=>row[5]=[...caps.slice(0,7),[0,1,2]]}))),error=>error.code==='TRACK_DIRECTORY_CAPABILITIES_INVALID');
});
test('remote errors retain bounded diagnostics and readonly adapter rejects publishing',async()=>{
 let remote=false;const session=await createTrackDirectoryReadSession(options(call=>reply(call,remote?{change:row=>{row[4]=422;row[5]=['not found'];}}:{})));
 remote=true;await assert.rejects(session.request(2,[]),error=>error.code==='TRACK_DIRECTORY_REMOTE_ERROR'&&error.status===422&&error.diagnostic==='not found');
 await assert.rejects(session.request(7,[]),error=>error.code==='TRACK_DIRECTORY_OPERATION_INVALID');session.close();
});
test('cancel, timeout and close settle even when transport ignores cancellation',async()=>{
 let hold=false,resolve,sent=0;const session=await createTrackDirectoryReadSession({...options(call=>{sent++;return hold?new Promise(done=>{resolve=()=>reply(call).then(done);}):reply(call);}),timeoutMs:50});
 hold=true;const abort=new AbortController(),pending=session.request(2,[],{signal:abort.signal});await Promise.resolve();abort.abort();await assert.rejects(pending,error=>error.name==='AbortError');resolve();
 await assert.rejects(session.request(2,[]),error=>error.code==='TRACK_DIRECTORY_TIMEOUT');
 const beforeClose=sent,closing=session.request(2,[]);session.close();await assert.rejects(closing,error=>error.code==='TRACK_DIRECTORY_CLOSED');assert.equal(sent,beforeClose,'close before send must not emit a request');
});
