import test from 'node:test';
import assert from 'node:assert/strict';
import {RegionUsbContext} from '../../site/tracks/src/region-usb-context.js';
const version=new Uint8Array(32).fill(1),hash=new Uint8Array(32).fill(2);
const caps=[1,'trailmate.track.regions.v1','test',version,3,32,1024,[9,10,11,12,13]];
const root=['test:r',null,0,'Root',[],[0,0,100,100],1],child=['test:c','test:r',1,'Child',[],[0,0,50,50],0];
test('USB scope is built only from verified capability, parent and boundary metadata',async()=>{
 const ctx=new RegionUsbContext('directory');
 assert.throws(()=>ctx.parameters(12,['test:r',version,0,1]),e=>e.code==='TRACK_REGION_REFERENCE_UNVERIFIED');
 await ctx.learn(9,[],caps);
 assert.deepEqual(ctx.parameters(10,[version,null,null,16]),['test',version,null,-1,null,3,16]);
 await ctx.learn(10,[version,null,null,16],[version,null,[root]]);
 assert.deepEqual(ctx.parameters(10,[version,'test:r',null,16]),['test',version,'test:r',0,null,3,16]);
 await ctx.learn(10,[version,'test:r',null,16],[version,null,[child]]);
 await ctx.learn(11,['test:r',version],[version,root,['region-e7-json-v1',hash,100]]);
 assert.deepEqual(ctx.parameters(12,['test:r',version,90,10]),['test','test:r',version,hash,100,90,10]);
 assert.deepEqual(ctx.parameters(13,['test:r',version,'intersects',null,null,0,16,9007199254740993n,null]),['test','test:r',version,[0,0,100,100],null,null,0,16,9007199254740993n,null]);
 assert.throws(()=>ctx.parameters(12,['test:c',version,0,1]),e=>e.code==='TRACK_REGION_REFERENCE_UNVERIFIED');
 assert.throws(()=>ctx.parameters(12,['test:r',version,99,2]),e=>e.code==='TRACK_REGION_REFERENCE_UNVERIFIED');
 assert.throws(()=>ctx.parameters(10,[version,'test:c','test:c',16]),e=>e.code==='TRACK_REGION_OPTIONS_INVALID');
});
test('same-version capability refresh preserves hierarchy; changed version discards old references',async()=>{
 const ctx=new RegionUsbContext('directory');await ctx.learn(9,[],caps);await ctx.learn(10,[version,null,null,16],[version,null,[root]]);
 await ctx.learn(9,[],caps);assert.equal(ctx.parameters(10,[version,'test:r',null,16])[3],0);
 const changed=new Uint8Array(32).fill(3);await ctx.learn(9,[],[...caps.slice(0,3),changed,...caps.slice(4)]);
 assert.throws(()=>ctx.parameters(10,[changed,'test:r',null,16]),e=>e.code==='TRACK_REGION_REFERENCE_UNVERIFIED');
 assert.throws(()=>ctx.parameters(11,['test:r',version]),e=>e.code==='TRACK_REGION_OPTIONS_INVALID');
});
test('wrong descriptor or uncorrelated boundary cannot poison a validated scope',async()=>{
 const ctx=new RegionUsbContext('directory');await ctx.learn(9,[],caps);
 await assert.rejects(ctx.learn(11,['test:r',version],[version,child,['region-e7-json-v1',hash,100]]),e=>e.code==='TRACK_REGION_DESCRIPTOR_INVALID');
 assert.throws(()=>ctx.parameters(12,['test:r',version,0,1]),e=>e.code==='TRACK_REGION_REFERENCE_UNVERIFIED');
 await ctx.learn(11,['test:r',version],[version,root,['region-e7-json-v1',hash,100]]);
 await assert.rejects(ctx.learn(12,['test:r',version,0,1],[hash,100,1,new Uint8Array(1)]),e=>e.code==='TRACK_REGION_BOUNDARY_CHUNK_INVALID');
});
