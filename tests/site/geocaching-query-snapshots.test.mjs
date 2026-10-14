import test from 'node:test';
import assert from 'node:assert/strict';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';
import {rememberQuerySnapshot,releaseQuerySnapshots} from '../../site/geocaching/src/query-snapshots.js';

const value=(length,byte)=>new Uint8Array(length).fill(byte);
function fixture(support=true){const client=new DirectoryClient(),entry={key:'directory',ready:true,epoch:value(16,1),queryReleaseSupported:support,limit:1};client.directories.set(entry.key,entry);return{client,entry,page:{directory:entry,bbox:[-900000000,-1800000000,900000000,1800000000],stateMask:3,cursor:null,snapshot:null},reply:[value(16,2),[],value(32,3),604800]};}

test('legacy directories never get release requests; completed leases need no release',async()=>{
 const {client,entry,page,reply}=fixture(false);rememberQuerySnapshot(client,page,reply);assert.equal(client.querySnapshots.size,0);
 entry.queryReleaseSupported=true;rememberQuerySnapshot(client,page,reply);reply[0][0]=9;assert.equal([...client.querySnapshots.values()][0].snapshot[0],2);
 reply[0]=value(16,2);reply[2]=null;rememberQuerySnapshot(client,page,reply);assert.equal(client.querySnapshots.size,0);
 client.request=()=>assert.fail('No lease to release');await releaseQuerySnapshots(client);
});

test('lost or malformed release replies retain one fixed retry ID and the original lease',async()=>{
 const {client,page,reply}=fixture();rememberQuerySnapshot(client,page,reply);const calls=[];let attempt=0;
 client.request=async(directory,operation,body,options)=>{calls.push({directory,operation,body,options});if(++attempt===1)throw Error('Lost response');if(attempt===2)return[value(16,9),1];return[body[0],0];};
 await assert.rejects(releaseQuerySnapshots(client),/Lost response/);assert.equal(client.querySnapshots.size,1);
 await assert.rejects(releaseQuerySnapshots(client),/Invalid query release/);assert.equal(client.querySnapshots.size,1);
 await releaseQuerySnapshots(client);assert.equal(client.querySnapshots.size,0);
 assert.ok(calls.every(call=>call.operation===6&&call.body[0][0]===2));
 assert.deepEqual(calls[0].options.requestId,calls[1].options.requestId);assert.deepEqual(calls[1].options.requestId,calls[2].options.requestId);
});

test('replaced directory or epoch is not addressed as the original lease owner',async()=>{
 for(const changed of ['entry','epoch','offline']){
  const {client,entry,page,reply}=fixture();rememberQuerySnapshot(client,page,reply);
  if(changed==='entry')client.directories.set(entry.key,{...entry});if(changed==='epoch')entry.epoch=value(16,7);if(changed==='offline')entry.ready=false;
  client.request=()=>assert.fail('Do not release against a different service');await releaseQuerySnapshots(client);assert.equal(client.querySnapshots.size,0);
 }
});

test('actual query replacement releases first, more preserves its current continuation',async()=>{
 const {client,entry}=fixture(),calls=[];let snapshot=2;
 const summary=byte=>[value(32,byte),1,value(32,4),0,300000000,1200000000,'Cache',2,2,0,128];
 client.request=async(directory,op,body)=>{calls.push(op);if(op===6)return[body[0],1];assert.equal(op,2);return[value(16,snapshot),[summary(snapshot+(body[4]===null?0:1))],body[4]===null?value(32,3):null,604800];};
 await client.query([-90,-180,90,180]);assert.deepEqual(calls,[2]);assert.equal(client.querySnapshots.size,1);
 snapshot++;await client.query([-90,-180,90,180],3,null,2,{refresh:true});assert.deepEqual(calls,[2,6,2]);assert.equal(client.querySnapshots.size,1);
 await client.more();assert.deepEqual(calls,[2,6,2,2]);assert.equal(client.querySnapshots.size,0);
 assert.equal(client.rows.size,2);assert.equal(entry.ready,true);
});

test('a superseded in-flight query is remembered and released before its replacement',async()=>{
 const {client}=fixture(),calls=[];let finish;const ready=new Promise(resolve=>{finish=resolve;});
 client.request=async(directory,op,body)=>{calls.push(op);if(op===6)return[body[0],1];if(calls.length===1)return ready;return[value(16,8),[],null,604800];};
 const first=client.query([-90,-180,90,180]);await Promise.resolve();await Promise.resolve();
 const second=client.query([20,110,40,130]);finish([value(16,2),[],value(32,3),604800]);
 await assert.rejects(first,/All directory queries failed/); // A continuation with no items is malformed.
 await second;assert.deepEqual(calls,[2,2]);
 // Repeat with a well-formed stale page: its lease must not disappear silently.
 const next=fixture(),seen=[];let complete;const delayed=new Promise(resolve=>{complete=resolve;});
 next.client.request=async(directory,op,body)=>{seen.push(op);if(op===6)return[body[0],1];return seen.length===1?delayed:[value(16,8),[],null,604800];};
 const pending=next.client.query([-90,-180,90,180]);await Promise.resolve();await Promise.resolve();const replacement=next.client.query([20,110,40,130]);
 complete([value(16,2),[[value(32,4),1,value(32,5),0,300000000,1200000000,'Cache',2,2,0,128]],value(32,3),604800]);
 await pending;await replacement;assert.deepEqual(seen,[2,6,2]);assert.equal(next.client.querySnapshots.size,0);
});

test('failed release prevents a new snapshot rather than accumulating abandoned work',async()=>{
 const {client,page,reply}=fixture();rememberQuerySnapshot(client,page,reply);const calls=[];
 client.request=async(_entry,op)=>{calls.push(op);throw Error('Release unavailable');};
 await assert.rejects(client.query([-90,-180,90,180]),/Release unavailable/);assert.deepEqual(calls,[6]);assert.equal(client.querySnapshots.size,1);
 await client.close();assert.equal(client.querySnapshots.size,0);
});

test('extended capability negotiation is explicit and preserves the legacy contract',async()=>{
 const legacy=[[1],[1],[0,1,2,3,4,5],8192,4096,64,604800,2592000,2,'Directory',1,2592000];
 for(const mode of ['old','new','mismatch','malformed']){
  const {client,entry}=fixture();const calls=[];
  client.request=async(_entry,op,body)=>{assert.equal(op,0);calls.push(body);if(!body.length)return legacy;if(mode==='old')throw Object.assign(Error('Old selector'),{status:400,reason:'malformed_request'});const extended=structuredClone(legacy);extended[2].push(6);if(mode==='mismatch')extended[5]=32;if(mode==='malformed')extended[2].push(6);return extended;};
  if(mode==='mismatch'||mode==='malformed')await assert.rejects(client.checkDirectory(entry),/not compatible/);
  else{await client.checkDirectory(entry);assert.equal(entry.ready,true);assert.equal(entry.receiptSupported,true);assert.equal(entry.queryReleaseSupported,mode==='new');}
  assert.deepEqual(calls,[[],[2]]);
 }
});
