import test from 'node:test';import assert from 'node:assert/strict';
import {retainDraftSave,loadDraftSave,clearDraftSave,assertDraftSaveRetained,withDraftSaveLock,draftSaveSnapshotMatches} from '../../site/geocaching/src/device-draft-save-journal.js';
import {locales} from '../../site/i18n/registry.js';import {geocachingSaveRecoveryText,geocachingSaveRecoveryKeys} from '../../site/i18n/geocaching-save-recovery-labels.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
const author=new Uint8Array(64).fill(1),owner=new Uint8Array(16).fill(2),id=new Uint8Array(16).fill(3),transferId=new Uint8Array(16).fill(4);
const record=()=>({authorKey:author.slice(),authorGeneration:7,owner:owner.slice(),id:id.slice(),transferId:transferId.slice(),expected:0,fields:{name:'中文保存 <script>不运行</script>',description:'正文\n'.repeat(60),hint:'提示',latitudeE7:310000000,longitudeE7:1210000000,state:0,difficultyX2:3,terrainX2:4,containerSize:2}});
const memory=()=>{const rows=new Map();return {rows,getItem:key=>rows.get(key)??null,setItem:(key,value)=>rows.set(key,value),removeItem:key=>rows.delete(key)};};
test('durable record freezes fields and full-width generation across reload without secrets',()=>{
 const storage=memory(),original=record();original.expected=9007199254740993n;const retained=retainDraftSave(original,{storage});original.fields.name='changed';original.owner.fill(0);original.authorKey.fill(0);
 const restored=loadDraftSave(author,{storage});assert.deepEqual(restored,retained);assert.equal(restored.expected,9007199254740993n);assert.equal(restored.fields.name,'中文保存 <script>不运行</script>');assert.equal(storage.rows.size,1);assert.ok(![...storage.rows.values()][0].includes('privateKey'));
 clearDraftSave(restored,{storage});assert.equal(loadDraftSave(author,{storage}),null);
});
test('one unresolved save per author prevents replacement and stale deletion',()=>{
 const storage=memory(),first=retainDraftSave(record(),{storage}),other=record();other.transferId[0]++;
 assert.throws(()=>retainDraftSave(other,{storage}),{code:'GEO_SAVE_RETAINED'});assert.deepEqual(loadDraftSave(author,{storage}),first);clearDraftSave(first,{storage});const next=retainDraftSave(other,{storage});assert.throws(()=>clearDraftSave(first,{storage}),{code:'GEO_SAVE_CHECKPOINT_STALE'});assert.deepEqual(loadDraftSave(author,{storage}),next);
 assert.equal(loadDraftSave(new Uint8Array(64).fill(9),{storage}),null);
 assert.throws(()=>assertDraftSaveRetained(first,{storage}),{code:'GEO_SAVE_CHECKPOINT_STALE'});assert.deepEqual(assertDraftSaveRetained(next,{storage}),next);clearDraftSave(next,{storage});assert.throws(()=>assertDraftSaveRetained(next,{storage}),{code:'GEO_SAVE_CHECKPOINT_STALE'});assert.equal(storage.rows.size,0);
});
test('restored small and full-width generations survive actual management encoding',()=>{
 for(const expected of [0,1,4294967296,Number.MAX_SAFE_INTEGER,9007199254740993n,0xfffffffffffffffen]){const source=record();source.expected=expected;const restored=retainDraftSave(source,{storage:memory()}),wire=encodeManagement(owner,id,66,[transferId,id,restored.expected,7]);assert.equal(BigInt(decodeManagement(wire).body[2]),BigInt(expected));if(BigInt(expected)<=BigInt(Number.MAX_SAFE_INTEGER))assert.equal(typeof restored.expected,'number');}
});
test('corrupt oversized and noncanonical records are retained and rejected',()=>{
 const mutations=[text=>'broken',text=>text+' ',text=>JSON.stringify({...JSON.parse(text),extra:1}),text=>text.replace('"expected":"0"','"expected":"00"'),text=>text.replace('"expected":"0"','"expected":"18446744073709551615"'),text=>text.replace('"authorGeneration":7','"authorGeneration":0'),text=>text.replace('"fields":{','"fields":{"unknown":1,'),text=>text.replace('"difficultyX2":3','"difficultyX2":99'),text=>'x'.repeat(8193)];
 for(const mutate of mutations){const storage=memory();retainDraftSave(record(),{storage});const [key,text]=[...storage.rows][0],damaged=mutate(text);storage.setItem(key,damaged);assert.throws(()=>loadDraftSave(author,{storage}));assert.equal(storage.getItem(key),damaged);}
 for(const property of ['owner','id','transferId']){const value=record();value[property].fill(0);assert.throws(()=>retainDraftSave(value,{storage:memory()}));}
});
test('storage failures never replace or silently clear the pending record',()=>{
 assert.throws(()=>retainDraftSave(record(),{storage:{getItem:()=>null,setItem(){throw Error('quota');}}}),{code:'GEO_SAVE_STORAGE'});
 const storage=memory(),retained=retainDraftSave(record(),{storage});storage.removeItem=()=>{};assert.throws(()=>clearDraftSave(retained,{storage}),{code:'GEO_SAVE_STORAGE'});assert.deepEqual(loadDraftSave(author,{storage}),retained);
});
test('query accepts only exact fields at expected+1 on the proven author',()=>{
 const retained=retainDraftSave(record(),{storage:memory()}),account={verified:true,publicKey:author,generation:7},snapshot={editable:true,draft:{...retained.fields,id,generation:1,authorKey:author,baseHash:new Uint8Array(32)}};
 assert.equal(draftSaveSnapshotMatches(retained,snapshot,account),true);
 assert.equal(draftSaveSnapshotMatches(retained,snapshot,{...account,generation:8}),true,'read-only reconciliation remains possible after identity restoration');
 for(const changed of [{generation:0},{generation:2},{name:'different'},{latitudeE7:0},{description:'different'},{id:new Uint8Array(16)}])assert.equal(draftSaveSnapshotMatches(retained,{...snapshot,draft:{...snapshot.draft,...changed}},account),false);
 assert.equal(draftSaveSnapshotMatches(retained,{...snapshot,editable:false},account),false);assert.equal(draftSaveSnapshotMatches(retained,snapshot,{...account,verified:false}),false);assert.equal(draftSaveSnapshotMatches(retained,snapshot,{...account,publicKey:new Uint8Array(64)}),false);
});
test('cross-tab lock rejects concurrent saves and unsupported locking before work',async()=>{
 let work=0;await assert.rejects(withDraftSaveLock(author,()=>work++,{locks:null}),{code:'GEO_SAVE_LOCK_UNAVAILABLE'});
 await assert.rejects(withDraftSaveLock(author,()=>work++,{locks:{request:async(name,options,action)=>{assert.match(name,/^trailmate-geo-draft-save-[a-f0-9]{64}$/);assert.equal(options.ifAvailable,true);return action(null);}}}),{code:'GEO_SAVE_BUSY'});assert.equal(work,0);
 assert.equal(await withDraftSaveLock(author,()=>++work,{locks:{request:async(name,options,action)=>action({name})}}),1);
});
test('save recovery catalog is complete and translated in all eleven locales',()=>{
 for(const {id} of locales){const text=geocachingSaveRecoveryText(id);assert.deepEqual(Object.keys(text),geocachingSaveRecoveryKeys);for(const value of Object.values(text))assert.ok(value.trim());if(id!=='en')assert.notEqual(text.query,geocachingSaveRecoveryText('en').query);}
});
