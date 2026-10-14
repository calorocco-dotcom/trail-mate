import test from 'node:test';
import assert from 'node:assert/strict';
import {readDeviceDraftPage} from '../../site/geocaching/src/device-draft-catalog.js';
const author={verified:true,publicKey:new Uint8Array(64).fill(2),generation:7};
const row=index=>[new Uint8Array(16).fill(index+1),1,index===1?new Uint8Array(64).fill(3):author.publicKey,'Draft '+index,0,null,null,null,[2,1,5]];
test('bounded pages bind catalog generation and preserve lower-bound count and old authors',async()=>{
  const calls=[];const session={async request(op,[offset,generation,epoch]){calls.push([op,offset,generation,epoch]);return [0n,offset,3,offset<3?row(offset):null];}};
  const first=await readDeviceDraftPage(session,author,{limit:2});
  assert.equal(first.hasMore,true);assert.equal(first.knownCount,3);assert.equal(first.rows[0].editable,true);assert.equal(first.rows[1].editable,false);
  assert.equal(first.rows[0].publication.pending,true);assert.equal(first.rows[0].publication.localChanges,true);
  const second=await readDeviceDraftPage(session,author,{offset:first.nextOffset,catalogGeneration:first.catalogGeneration});
  assert.equal(second.rows.length,1);assert.equal(second.hasMore,false);assert.equal(second.nextOffset,3);
  assert.deepEqual(calls,[[64,0,null,7],[64,1,0n,7],[64,2,0n,7],[64,3,0n,7]]);
});
test('changed generation and duplicate rows reject the whole page',async()=>{
  await assert.rejects(readDeviceDraftPage({async request(op,[offset]){return [offset?2:1,offset,3,row(offset)];}},author),/翻页期间变化/);
  await assert.rejects(readDeviceDraftPage({async request(op,[offset]){return [1,offset,3,row(0)];}},author),/重复/);
});
test('invalid end markers, coordinates and publication facts are refused',async()=>{
  await assert.rejects(readDeviceDraftPage({async request(){return [1,0,1,null];}},author),/结束标记/);
  for(const invalid of [Object.assign(row(0),{5:1}),Object.assign(row(0),{8:[1,2,0]}),Object.assign(row(0),{3:'x'.repeat(97)})]){
    await assert.rejects(readDeviceDraftPage({async request(){return [1,0,1,invalid];}},author),/目录行无效/);
  }
});
test('unbound continuation and excessive page sizes never issue USB commands',async()=>{
  const session={async request(){assert.fail('unexpected USB request');}};
  await assert.rejects(readDeviceDraftPage(session,author,{offset:1}),/分页参数/);
  await assert.rejects(readDeviceDraftPage(session,author,{limit:17}),/分页参数/);
});
test('Busy is bounded and cancellation during final reply cannot publish a page',async()=>{
  let clock=0;const busy={async request(){const e=Error('busy');e.code=5;throw e;}};
  await assert.rejects(readDeviceDraftPage(busy,author,{timeoutMs:250,now:()=>clock,sleep:async ms=>{clock+=ms;}}),/超时/);
  const controller=new AbortController();
  await assert.rejects(readDeviceDraftPage({async request(){controller.abort();return [1,0,0,null];}},author,{signal:controller.signal}),{name:'AbortError'});
});
