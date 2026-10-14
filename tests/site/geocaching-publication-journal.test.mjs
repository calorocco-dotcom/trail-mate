import test from 'node:test';
import assert from 'node:assert/strict';
import {savePublicationCheckpoint,loadPublicationCheckpoint,managementOwner} from '../../site/geocaching/src/publication-journal.js';
const storage=()=>{const map=new Map();return {map,getItem:key=>map.get(key)??null,setItem:(key,value)=>map.set(key,value)};};
const fixture=()=>({stage:'submitting',title:'设备草稿',owner:new Uint8Array(16).fill(8),prepared:{prepareId:new Uint8Array(16).fill(1),id:new Uint8Array(16).fill(2),
  generation:0xffffffffffffffffn,authorKey:new Uint8Array(64).fill(3),authorGeneration:7,directory:new Uint8Array(16).fill(4),directoryEpoch:new Uint8Array(16).fill(5),
  contentHash:new Uint8Array(32).fill(6),fromRevision:0,toRevision:1,expiresAt:5000}});

test('retains correlation owner and full uint64 generation while invalidating old monotonic expiry on reload',()=>{
  const record=fixture(),s=storage();savePublicationCheckpoint(record,s);
  const loaded=loadPublicationCheckpoint(record.prepared.authorKey,s);
  assert.deepEqual(loaded.owner,record.owner);assert.equal(loaded.stage,'submitting');assert.equal(loaded.prepared.generation,0xffffffffffffffffn);
  assert.equal(loaded.prepared.expiresAt,0);assert.equal(loaded.restored,true);
});

test('reload preserves an unconfirmed stop as an unresolved original operation',()=>{
  const record=fixture(),s=storage();record.stage='stopping';savePublicationCheckpoint(record,s);
  const loaded=loadPublicationCheckpoint(record.prepared.authorKey,s);
  assert.equal(loaded.stage,'stopping');assert.deepEqual(loaded.prepared.prepareId,record.prepared.prepareId);
  assert.equal(loaded.restored,true);assert.equal(loaded.prepared.expiresAt,0);
});

test('serializes only public correlation metadata even if an input contains secrets or draft content',()=>{
  const record=fixture(),s=storage();record.privateKey='never-persist';record.prepared.privateKey='never-persist';record.description='private draft body';
  savePublicationCheckpoint(record,s);const raw=[...s.map.values()][0];
  assert.equal(raw.includes('never-persist'),false);assert.equal(raw.includes('private draft body'),false);assert.equal(raw.includes('expiresAt'),false);
});

test('isolates device authors and does not remove corrupted records',()=>{
  const record=fixture(),s=storage();savePublicationCheckpoint(record,s);
  assert.equal(loadPublicationCheckpoint(new Uint8Array(64).fill(9),s),undefined);
  const key=[...s.map.keys()][0];s.map.set(key,'{broken');
  assert.throws(()=>loadPublicationCheckpoint(record.prepared.authorKey,s));assert.equal(s.map.get(key),'{broken');
});

test('rejects malformed persisted operation IDs, versions, author bindings and owners',()=>{
  const record=fixture(),s=storage();savePublicationCheckpoint(record,s);const key=[...s.map.keys()][0],raw=s.map.get(key);
  for(const mutate of [v=>v.version=2,v=>v.prepared.prepareId='00'.repeat(16),v=>v.prepared.generation='01',v=>v.prepared.authorKey='09'.repeat(64),
    v=>v.prepared.toRevision=3,v=>v.owner='00'.repeat(16),v=>v.title='汉'.repeat(33)]){
    const value=JSON.parse(raw);mutate(value);s.map.set(key,JSON.stringify(value));
    assert.throws(()=>loadPublicationCheckpoint(record.prepared.authorKey,s));
  }
});

test('storage write failures propagate before callers can send a new submission',()=>{
  assert.throws(()=>savePublicationCheckpoint(fixture(),{setItem(){throw Error('quota');}}),/无法更新/);
});

test('unsafe numeric generations cannot be rounded into a retained operation',()=>{
  const record=fixture();record.prepared.generation=Number.MAX_SAFE_INTEGER+1;
  assert.throws(()=>savePublicationCheckpoint(record,storage()));
});

test('owner creation survives reload and retains corrupt owner data rather than replacing it',()=>{
  const s=storage(),id=new Uint8Array(16).fill(8);
  assert.deepEqual(managementOwner(s,()=>id),id);assert.deepEqual(managementOwner(s,()=>{throw Error('must reuse');}),id);
  const key=[...s.map.keys()][0];s.map.set(key,'broken');assert.throws(()=>managementOwner(s,()=>id));assert.equal(s.map.get(key),'broken');
});
