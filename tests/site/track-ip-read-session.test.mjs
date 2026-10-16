import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {TrackDirectoryClient} from '../../site/tracks/src/protocol-client.js';
import {createIPTrackReadSession} from '../../site/tracks/src/ip-read-session.js';

test('IP read adapter pins directory and freezes requests without device network commands',async()=>{
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),delivery=new Uint8Array(16).fill(2),epoch=new Uint8Array(16).fill(3);
 const directory={key:'directory',capabilitiesVerified:true,publicKey,delivery,epoch},network=new TrackDirectoryClient(),entry={identity,destination:delivery,epoch,ready:true};network.directories.set(directory.key,entry);
 let reads=0;network.request=async(target,op,body)=>{reads++;assert.equal(target,entry);assert.equal(op,2);assert.equal(body[0][0],4);return ['body'];};
 const reader=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}}),id=new Uint8Array(32).fill(4),pending=reader.request(2,[id,null]);id.fill(9);
 assert.deepEqual(await pending,['body']);assert.equal(reads,1);assert.equal(reader.capabilities.chunkBytes,1024);
 network.generation++;assert.throws(()=>reader.request(2,[id,null]),/context changed/);assert.equal(reads,1);
 const next=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});next.close();assert.throws(()=>next.request(2,[id,null]),/context changed/);
 await assert.rejects(createIPTrackReadSession({network,directory:{...directory,publicKey:new Uint8Array(64)},assertCurrent:()=>{}}),/binding changed/);
 const fresh=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});network.request=async()=>{network.generation++;return ['stale'];};await assert.rejects(fresh.request(2,[id,null]),/context changed/);
});
test('package status is read-only and requires the selected directory capability',async()=>{
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),delivery=new Uint8Array(16).fill(3),epoch=new Uint8Array(16).fill(4),network=new TrackDirectoryClient();
 const directory={key:'package',capabilitiesVerified:true,publicKey,delivery,epoch},entry={identity,destination:delivery,epoch,ready:true,operations:[]};network.directories.set(directory.key,entry);let calls=0;
 network.request=async(target,op)=>{assert.equal(target,entry);assert.equal(op,14);calls++;return ['status'];};
 const legacy=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});assert.equal(legacy.capabilities.packageSupported,false);assert.throws(()=>legacy.request(14,[]),error=>error.code==='TRACK_PACKAGE_UNSUPPORTED');assert.equal(calls,0);
 entry.operations=[14];const capable=await createIPTrackReadSession({network,directory,assertCurrent:()=>{}});assert.equal(capable.capabilities.packageSupported,true);assert.deepEqual(await capable.request(14,[]),['status']);assert.equal(calls,1);assert.throws(()=>capable.request(7,[]),/Unsupported/);
});
