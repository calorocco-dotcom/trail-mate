import test from 'node:test';
import assert from 'node:assert/strict';
import {DeviceDraftReader} from '../../site/geocaching/src/device-draft-reader.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';
import {encode} from '../../site/geocaching/src/protocol.js';
const author={verified:true,publicKey:new Uint8Array(64).fill(2),generation:7};
const raw=encode([null,null,[0,null,null,'Draft','','',2,2,0],1]);
const id=new Uint8Array(16).fill(1);
const deferred=()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};};

test('switch waits for old response and confirmed release before new device read',async()=>{
  const reply=deferred(),closing=deferred(),started=deferred(),releaseStarted=deferred();const calls=[];
  const session={async request(op,body){calls.push(op);
    if(calls.length===1){started.resolve();return reply.promise;}
    if(op===OPERATION.SESSION_CLOSE&&calls.length===2){releaseStarted.resolve();await closing.promise;return [];}
    if(op===OPERATION.SESSION_CLOSE)return [];
    return [body[0],1,raw.length,0,raw];
  }};
  const reader=new DeviceDraftReader(session);
  const old=reader.read(id,author);const rejected=assert.rejects(old,{name:'AbortError'});
  await started.promise;
  const next=reader.read(new Uint8Array(16).fill(3),author);
  reply.resolve([id,1,raw.length,0,raw]);await releaseStarted.promise;
  assert.deepEqual(calls,[OPERATION.CACHE_DRAFT_GET,OPERATION.SESSION_CLOSE]);
  closing.resolve();await rejected;
  assert.equal((await next).draft.id[0],3);
  assert.deepEqual(calls,[65,3,65,3]);await reader.close();
});

test('release failure blocks further reads until a new connection owns the lease',async()=>{
  const calls=[];const session={async request(op){calls.push(op);if(op===3)throw Error('USB disconnected');return [id,1,raw.length,0,raw];}};
  const reader=new DeviceDraftReader(session);
  await assert.rejects(reader.read(id,author),/释放未确认/);
  await assert.rejects(reader.read(id,author),/释放未确认/);
  assert.deepEqual(calls,[65,3]);await reader.close();
  await assert.rejects(reader.read(id,author),/已关闭/);
});

test('failed decoding still releases the device job and preserves the original error',async()=>{
  const calls=[];const session={async request(op){calls.push(op);return op===3?[]:[id,1,1,0,new Uint8Array([0])];}};
  const reader=new DeviceDraftReader(session);
  await assert.rejects(reader.read(id,author),/草稿格式无效/);
  assert.deepEqual(calls,[65,3]);await reader.close();
});

test('invalid release acknowledgement cannot unlock the next read',async()=>{
  const calls=[];const session={async request(op){calls.push(op);return op===3?[true]:[id,1,raw.length,0,raw];}};
  const reader=new DeviceDraftReader(session);
  await assert.rejects(reader.read(id,author),/释放未确认/);
  await assert.rejects(reader.read(id,author),/释放未确认/);
  assert.deepEqual(calls,[65,3]);
});

test('catalog and draft reads share one ordered maintenance owner',async()=>{
  const calls=[];const session={async request(op,body){calls.push(op);
    if(op===3)return [];
    if(op===64)return [0,body[0],0,null];
    return [id,1,raw.length,0,raw];
  }};
  const reader=new DeviceDraftReader(session);
  const page=await reader.list(author);
  assert.equal(page.rows.length,0);assert.equal(page.hasMore,false);
  assert.equal((await reader.read(id,author)).draft.name,'Draft');
  assert.deepEqual(calls,[64,3,65,3]);await reader.close();
});

test('save waits for a matching receipt and releases the same maintenance owner',async()=>{
  const transferId=new Uint8Array(16).fill(4),calls=[];
  const session={async request(op,body){calls.push(op);
    if(op===32)return [transferId,320,body[2],1];
    if(op===66)return [transferId,id,1];
    assert.equal(op,3);return [];
  }};
  const reader=new DeviceDraftReader(session);
  const saved=await reader.save(id,{name:'New draft'},0,author,{transferId});
  assert.equal(saved.generation,1);assert.deepEqual(calls,[32,66,3]);await reader.close();
});
