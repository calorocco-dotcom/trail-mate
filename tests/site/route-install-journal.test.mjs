import test from 'node:test';import assert from 'node:assert/strict';
import {loadRouteInstallCheckpoint,saveRouteInstallCheckpoint,forgetRouteInstallCheckpoint,routeInstallUnresolved} from '../../site/tracks/src/route-install-journal.js';
const memory=()=>{const values=new Map();return {values,getItem:key=>values.get(key)??null,setItem:(key,value)=>values.set(key,value),removeItem:key=>values.delete(key)};};
const fixture=()=>({id:new Uint8Array(16).fill(1),sha256:new Uint8Array(32).fill(2),authorKey:new Uint8Array(64).fill(3),bytes:4096,generation:7,stage:'submitting'});
test('bounded public checkpoint survives reload and stays separate for each device author',()=>{
 const storage=memory(),record=fixture();saveRouteInstallCheckpoint({...record,privateKey:'must-not-save',source:'must-not-save'},{storage});
 assert.deepEqual(loadRouteInstallCheckpoint(record.authorKey,{storage}),record);assert.equal(loadRouteInstallCheckpoint(new Uint8Array(64).fill(4),{storage}),null);
 const raw=[...storage.values.values()][0];assert.ok(raw.length<1024);assert.equal(raw.includes('must-not-save'),false);
 record.id.fill(9);assert.equal(loadRouteInstallCheckpoint(record.authorKey,{storage}).id[0],1);
});
test('unresolved work cannot be overwritten by another installation, but stages may progress',()=>{
 const storage=memory(),record=fixture();saveRouteInstallCheckpoint(record,{storage});
 assert.throws(()=>saveRouteInstallCheckpoint({...record,id:new Uint8Array(16).fill(4)},{storage}),{code:'ROUTE_INSTALL_PENDING'});
 const finished=saveRouteInstallCheckpoint({...record,stage:'installed'},{storage});assert.equal(routeInstallUnresolved(finished),false);
 saveRouteInstallCheckpoint({...record,id:new Uint8Array(16).fill(4),stage:'uploading'},{storage});assert.equal(routeInstallUnresolved(loadRouteInstallCheckpoint(record.authorKey,{storage})),true);
 forgetRouteInstallCheckpoint(record.authorKey,{storage});assert.equal(loadRouteInstallCheckpoint(record.authorKey,{storage}),null);
});
test('corruption and unavailable persistence are rejected rather than silently dropping recovery',()=>{
 const storage=memory(),record=fixture();saveRouteInstallCheckpoint(record,{storage});const key=[...storage.values.keys()][0];
 for(const raw of ['{',JSON.stringify({schema:2}),'x'.repeat(1025)]){storage.values.set(key,raw);assert.throws(()=>loadRouteInstallCheckpoint(record.authorKey,{storage}),{code:'ROUTE_CHECKPOINT_INVALID'});}
 const unavailable={getItem(){throw Error('denied');}};assert.throws(()=>saveRouteInstallCheckpoint(record,{storage:unavailable}),{code:'ROUTE_CHECKPOINT_STORAGE'});
 const ignored={getItem(){return null;},setItem(){}};assert.throws(()=>saveRouteInstallCheckpoint(record,{storage:ignored}),{code:'ROUTE_CHECKPOINT_STORAGE'});
});
test('stale panels cannot clear a newer task or a newer checkpoint stage',()=>{
 const storage=memory(),old={...fixture(),stage:'installed'};saveRouteInstallCheckpoint(old,{storage});
 const next={...old,id:new Uint8Array(16).fill(8),stage:'uploading'};saveRouteInstallCheckpoint(next,{storage});
 assert.throws(()=>forgetRouteInstallCheckpoint(old.authorKey,{storage,expected:old}),{code:'ROUTE_CHECKPOINT_STALE'});assert.deepEqual(loadRouteInstallCheckpoint(old.authorKey,{storage}),next);
 const admitted=saveRouteInstallCheckpoint({...next,stage:'admitted'},{storage});assert.throws(()=>forgetRouteInstallCheckpoint(next.authorKey,{storage,expected:next}),{code:'ROUTE_CHECKPOINT_STALE'});
 forgetRouteInstallCheckpoint(admitted.authorKey,{storage,expected:admitted});assert.equal(loadRouteInstallCheckpoint(old.authorKey,{storage}),null);
});
