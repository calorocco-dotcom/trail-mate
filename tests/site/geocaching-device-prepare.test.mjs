import test from 'node:test';
import assert from 'node:assert/strict';
import {prepareDeviceDraft} from '../../site/geocaching/src/device-draft-prepare.js';
import {DeviceDraftReader} from '../../site/geocaching/src/device-draft-reader.js';
import {decodeDeviceDraft} from '../../site/geocaching/src/device-draft.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';

async function prepareFixture({generation=1,base=null,owner=null,coordinates=true}={}){
  const id=new Uint8Array(16).fill(1),publicKey=new Uint8Array(64).fill(2);
  const raw=encode([owner,base,[0,coordinates?305000000:null,coordinates?1205000000:null,'Shared cache','设备上的内容','提示',2,3,0],generation]);
  return {author:{verified:true,publicKey,generation:7},snapshot:{draft:decodeDeviceDraft(id,raw),bytes:raw},
    directory:{capabilitiesVerified:true,delivery:new Uint8Array(16).fill(4),epoch:new Uint8Array(16).fill(5)},
    prepareId:new Uint8Array(16).fill(3),hash:new Uint8Array(await crypto.subtle.digest('SHA-256',raw))};
}
function prepareReply(f,remaining=120000){
  return [f.prepareId.slice(),f.snapshot.draft.id.slice(),f.snapshot.draft.generation,f.author.publicKey.slice(),
    f.directory.delivery.slice(),f.directory.epoch.slice(),f.hash.slice(),f.snapshot.draft.baseHash?4:0,f.snapshot.draft.baseHash?5:1,remaining];
}

test('prepares the exact committed snapshot with one retained ID across Busy',async()=>{
  const f=await prepareFixture(),calls=[],phases=[];let count=0;
  const session={async request(op,body){calls.push([op,body]);if(!count++){const error=Error('busy');error.code=5;throw error;}return prepareReply(f);}};
  const result=await prepareDeviceDraft(session,f.snapshot,f.directory,f.author,{prepareId:f.prepareId,sleep:async()=>{},onProgress:p=>phases.push(p.phase)});
  for(const [op,body] of calls){assert.equal(op,67);assert.deepEqual(body,[f.snapshot.draft.id,1,f.directory.delivery,7,f.prepareId]);}
  assert.deepEqual(phases,['checking','prepared']);assert.deepEqual(result.contentHash,f.hash);assert.equal(result.toRevision,1);
  assert.equal(result.authorGeneration,7);assert.deepEqual(result.directoryEpoch,f.directory.epoch);
});

test('input buffers and author identity are frozen before the first asynchronous step',async()=>{
  const f=await prepareFixture(),reply=prepareReply(f),expected=[f.snapshot.draft.id.slice(),1,f.directory.delivery.slice(),7,f.prepareId.slice()];
  const session={async request(op,body){assert.equal(op,67);assert.deepEqual(body,expected);return reply;}};
  const work=prepareDeviceDraft(session,f.snapshot,f.directory,f.author,{prepareId:f.prepareId});
  f.snapshot.draft.id.fill(9);f.snapshot.bytes.fill(0);f.snapshot.draft.generation=10;
  f.author.publicKey.fill(9);f.author.generation=8;f.directory.delivery.fill(9);f.directory.epoch.fill(9);f.prepareId.fill(9);
  const result=await work;assert.equal(result.id[0],1);assert.equal(result.authorKey[0],2);assert.equal(result.authorGeneration,7);
});

test('rejects unverified authors, foreign drafts, incomplete publications and unsaved snapshots without device I/O',async()=>{
  let calls=0;const session={async request(){calls++;throw Error('unexpected request');}},f=await prepareFixture();
  await assert.rejects(prepareDeviceDraft(session,f.snapshot,f.directory,{...f.author,verified:false},{prepareId:f.prepareId}),/作者身份/);
  const foreign=await prepareFixture({owner:new Uint8Array(64).fill(9)});
  await assert.rejects(prepareDeviceDraft(session,foreign.snapshot,foreign.directory,foreign.author,{prepareId:foreign.prepareId}),/其他作者/);
  const incomplete=await prepareFixture({coordinates:false});
  await assert.rejects(prepareDeviceDraft(session,incomplete.snapshot,incomplete.directory,incomplete.author,{prepareId:incomplete.prepareId}),/坐标/);
  f.snapshot.draft.generation=2;
  await assert.rejects(prepareDeviceDraft(session,f.snapshot,f.directory,f.author,{prepareId:f.prepareId}),/代次不一致/);
  assert.equal(calls,0);
});

test('rejects an empty ID and an unverified directory before requests',async()=>{
  const f=await prepareFixture(),session={async request(){throw Error('unexpected request');}};
  await assert.rejects(prepareDeviceDraft(session,f.snapshot,f.directory,f.author,{prepareId:new Uint8Array(16)}),/随机准备 ID/);
  await assert.rejects(prepareDeviceDraft(session,f.snapshot,{...f.directory,capabilitiesVerified:false},f.author,{prepareId:f.prepareId}),/已验证/);
});

test('rejects every correlation and version/expiry mismatch in confirmation',async()=>{
  const f=await prepareFixture();
  const changes=[r=>r.pop(),r=>r[0][0]^=1,r=>r[1][0]^=1,r=>r[2]=2,r=>r[3][0]^=1,r=>r[4][0]^=1,
    r=>r[5][0]^=1,r=>r[6][0]^=1,r=>r[7]=-1,r=>r[8]=3,r=>r[9]=0,r=>r[9]=120001,r=>{r[7]=1;r[8]=2;}];
  for(const change of changes){const reply=prepareReply(f);change(reply);
    await assert.rejects(prepareDeviceDraft({async request(){return reply;}},f.snapshot,f.directory,f.author,{prepareId:f.prepareId}),/回执无效|绑定/);}
});

test('preserves full uint64 draft generation and validates an update with retained base metadata',async()=>{
  const f=await prepareFixture({generation:0x8000000000000001n,base:new Uint8Array(32).fill(6),owner:new Uint8Array(64).fill(2)});
  const session={async request(op,body){assert.equal(body[1],0x8000000000000001n);return prepareReply(f);}};
  const result=await prepareDeviceDraft(session,f.snapshot,f.directory,f.author,{prepareId:f.prepareId});
  assert.equal(result.generation,0x8000000000000001n);assert.equal(result.fromRevision,4);assert.equal(result.toRevision,5);
});

test('bounded waiting keeps the ID and propagates terminal identity/conflict errors',async()=>{
  const f=await prepareFixture();let clock=0,calls=0;
  await assert.rejects(prepareDeviceDraft({async request(op,body){calls++;assert.deepEqual(body[4],f.prepareId);const e=Error('busy');e.code=5;throw e;}},
    f.snapshot,f.directory,f.author,{prepareId:f.prepareId,timeoutMs:20,retryMs:10,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/尚未确认/);
  assert.equal(calls,2);
  const stale=Error('identity changed');stale.code=9;
  await assert.rejects(prepareDeviceDraft({async request(){throw stale;}},f.snapshot,f.directory,f.author,{prepareId:f.prepareId}),error=>error===stale);
});

test('conservative expiry includes round-trip time and rejects a reply that has already expired',async()=>{
  const f=await prepareFixture();let clock=100;
  const session={async request(){clock=160;return prepareReply(f,100);}};
  const result=await prepareDeviceDraft(session,f.snapshot,f.directory,f.author,{prepareId:f.prepareId,now:()=>clock});
  assert.equal(result.expiresAt,200);assert.equal(result.remainingMs,40);
  clock=100;
  await assert.rejects(prepareDeviceDraft({async request(){clock=160;return prepareReply(f,50);}},f.snapshot,f.directory,f.author,
    {prepareId:f.prepareId,now:()=>clock}),/已过期/);
});

test('cancellation arriving with a successful reply never reports prepared',async()=>{
  const f=await prepareFixture(),controller=new AbortController(),phases=[];
  const session={async request(){controller.abort();return prepareReply(f);}};
  await assert.rejects(prepareDeviceDraft(session,f.snapshot,f.directory,f.author,{prepareId:f.prepareId,signal:controller.signal,onProgress:p=>phases.push(p.phase)}),{name:'AbortError'});
  assert.deepEqual(phases,['checking']);
});

test('shared reader releases preparation resources before the next read',async()=>{
  const f=await prepareFixture(),calls=[];
  const session={async request(op,body){calls.push(op);if(op===3)return [];if(op===67)return prepareReply(f);
    return [body[0],1,f.snapshot.bytes.length,0,f.snapshot.bytes];}};
  const reader=new DeviceDraftReader(session);
  await reader.prepare(f.snapshot,f.directory,f.author,{prepareId:f.prepareId});
  await reader.read(f.snapshot.draft.id,f.author);
  assert.deepEqual(calls,[67,3,65,3]);await reader.close();
});

test('cancellation from the progress callback cannot return a confirmation',async()=>{
  const f=await prepareFixture(),controller=new AbortController();
  await assert.rejects(prepareDeviceDraft({async request(){return prepareReply(f);}},f.snapshot,f.directory,f.author,
    {prepareId:f.prepareId,signal:controller.signal,onProgress:p=>{if(p.phase==='prepared')controller.abort();}}),{name:'AbortError'});
});
