import test from 'node:test';
import assert from 'node:assert/strict';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {encodeDraftEdit,decodeDeviceDraft,validateDraftPublication,validateDraftFields} from '../../site/geocaching/src/device-draft.js';
const id=new Uint8Array(16).fill(1);
test('validation exposes stable codes without relaxing draft constraints',()=>{
 for(const [fields,code] of [[null,'GEO_DRAFT_FIELDS'],[{state:3},'GEO_DRAFT_OPTIONS'],[{latitudeE7:1},'GEO_DRAFT_COORDINATE_PAIR'],[{latitudeE7:900000001,longitudeE7:0},'GEO_DRAFT_COORDINATE_RANGE'],[{description:'中'.repeat(700)},'GEO_DRAFT_TEXT']])assert.throws(()=>validateDraftFields(fields),{code});
});
test('device drafts preserve optional coordinates and the existing four-field wire',()=>{
  const raw=encodeDraftEdit(id,{name:'山间藏宝点',description:'第一行\n第二行',hint:'树下'},0);
  assert.deepEqual(decode(raw),[null,null,[0,null,null,'山间藏宝点','第一行\n第二行','树下',2,2,0],1]);
  const draft=decodeDeviceDraft(id,raw);assert.equal(draft.generation,1);assert.equal(draft.latitudeE7,null);
  assert.throws(()=>validateDraftPublication(draft),/坐标/);
});
test('author metadata is read-only while saved draft generations advance exactly once',()=>{
  const key=new Uint8Array(64).fill(2),base=new Uint8Array(32).fill(3);
  const raw=encode([key,base,[0,100,200,'Spot','','',3,4,2],9]);
  const draft=decodeDeviceDraft(id,raw);assert.deepEqual(draft.authorKey,key);assert.deepEqual(draft.baseHash,base);
  assert.throws(()=>encodeDraftEdit(id,draft,draft.generation),/作者与基版本/);
  const {authorKey,baseHash,...editable}=draft;
  assert.equal(decodeDeviceDraft(id,encodeDraftEdit(id,editable,9)).generation,10);
  assert.equal(validateDraftPublication(editable).name,'Spot');
});
test('UTF-8 limits, paired coordinates and invalid fields are enforced before upload',()=>{
  assert.throws(()=>encodeDraftEdit(id,{name:'山'.repeat(33)},0),/文本/);
  assert.throws(()=>encodeDraftEdit(id,{name:'bad\0'},0),/文本/);
  assert.throws(()=>encodeDraftEdit(id,{latitudeE7:0},0),/同时/);
  assert.throws(()=>encodeDraftEdit(id,{latitudeE7:0,longitudeE7:1800000000},0),/坐标/);
  assert.throws(()=>encodeDraftEdit(id,{difficultyX2:11},0),/难度/);
  assert.throws(()=>validateDraftPublication({latitudeE7:0,longitudeE7:0,name:'  '}),/名称/);
});
test('uint64 draft generations remain exact and cannot overflow on save',()=>{
  const current=9007199254740993n;
  assert.equal(decodeDeviceDraft(id,encodeDraftEdit(id,{},current)).generation,current+1n);
  assert.throws(()=>encodeDraftEdit(id,{},0xffffffffffffffffn),/耗尽/);
  assert.throws(()=>encodeDraftEdit(id,{},-1),/范围/);
  assert.throws(()=>decodeDeviceDraft(id,encode([null,null,[0,null,null,'','','',2,2,0],0])),/范围/);
});
