import test from 'node:test';
import assert from 'node:assert/strict';
import {deviceUsbOwner} from '../../site/geocaching/src/device-usb-owner.js';
const author={verified:true,publicKey:new Uint8Array(64).fill(1),generation:2};
const locks={request:async(_key,_options,work)=>work()};
function storage(){const data=new Map();return {data,getItem:key=>data.get(key)??null,setItem:(key,value)=>data.set(key,value)};}
test('Proved device keeps its USB correlation across reload and device changes',async()=>{
 const saved=storage(),options={storage:saved,locks,random:()=>new Uint8Array(16).fill(7)};
 const first=await deviceUsbOwner(author,options);first[0]=99;
 const reloaded=await deviceUsbOwner(author,{...options,random:()=>{throw Error('Must not rotate');}});
 assert.deepEqual(reloaded,new Uint8Array(16).fill(7));
 const other={...author,publicKey:new Uint8Array(64).fill(2)};
 const oldSaveOwner=new Uint8Array(16).fill(9);
 assert.deepEqual(await deviceUsbOwner(other,{...options,preferred:oldSaveOwner}),oldSaveOwner);
 assert.deepEqual(await deviceUsbOwner(author,options),reloaded);
 assert.deepEqual(await deviceUsbOwner(other,options),oldSaveOwner);
 assert.equal(saved.data.size,3); // legacy correlation plus two proved accounts
});
test('Stored owner conflict, corruption and failed retention do not create replacement uploads',async()=>{
 const saved=storage(),options={storage:saved,locks,random:()=>new Uint8Array(16).fill(7)};
 await deviceUsbOwner(author,options);
 await assert.rejects(deviceUsbOwner(author,{...options,preferred:new Uint8Array(16).fill(9)}),{code:'GEO_SAVE_OWNER_CONFLICT'});
 const key=[...saved.data.keys()].find(key=>key.includes('device-usb-owner'));
 saved.data.set(key,'broken');const before=new Map(saved.data);
 await assert.rejects(deviceUsbOwner(author,options),{code:'GEO_SAVE_CHECKPOINT_INVALID'});
 assert.deepEqual(saved.data,before);
 await assert.rejects(deviceUsbOwner({...author,verified:false},options),{code:'GEO_SAVE_CHECKPOINT_INVALID'});
 await assert.rejects(deviceUsbOwner(author,{...options,locks:null}),{code:'GEO_SAVE_LOCK_UNAVAILABLE'});
 await assert.rejects(deviceUsbOwner(author,{...options,storage:{getItem(){throw Error('Storage unavailable');}}}),{code:'GEO_SAVE_STORAGE'});
 await assert.rejects(deviceUsbOwner(author,{...options,storage:{getItem(){return null;},setItem(){}}}),{code:'GEO_SAVE_STORAGE'});
});
test('Concurrent first connections serialize retention of the same device owner',async()=>{
 const saved=storage();let tail=Promise.resolve();
 const serialized={request:(_key,_options,work)=>{const result=tail.then(work);tail=result.catch(()=>{});return result;}};
 let count=0;
 const options={storage:saved,locks:serialized,random:()=>new Uint8Array(16).fill(++count)};
 const [left,right]=await Promise.all([deviceUsbOwner(author,options),deviceUsbOwner(author,options)]);
 assert.deepEqual(left,right);assert.equal(count,1);
});
