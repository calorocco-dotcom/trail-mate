import test from 'node:test';import assert from 'node:assert/strict';
import {installUploadedRoute,installedRoutePath,queryRouteInstallation} from '../../site/tracks/src/device-route-install.js';
const fixture=()=>({id:new Uint8Array(16).fill(1),sha256:new Uint8Array(32).fill(2),bytes:1024,sealed:true,published:false});
const author={verified:true,publicKey:new Uint8Array(64).fill(3),generation:7};
test('installation consumes an exact upload binding and accepts only content-addressed receipt',async()=>{
 const upload=fixture();let calls=0;
 const result=await installUploadedRoute({async request(op,body){calls++;assert.equal(op,85);assert.deepEqual(body,[upload.id,upload.sha256,1024,7]);return [body[0],body[1],1024,installedRoutePath(body[1]),1];}},upload,author);
 assert.equal(calls,1);assert.equal(result.installed,true);assert.equal(result.path,'/routes/'+'02'.repeat(32)+'.kml');
});
test('missing seal, author proof and cancelled operation send no request',async()=>{
 let calls=0;const session={async request(){calls++;}};
 for(const upload of [{...fixture(),sealed:false},{...fixture(),id:new Uint8Array(16)},{...fixture(),bytes:8388609}])await assert.rejects(installUploadedRoute(session,upload,author),{code:'ROUTE_UPLOAD_INVALID'});
 await assert.rejects(installUploadedRoute(session,fixture(),{...author,verified:false}),{code:'ROUTE_AUTHOR_INVALID'});
 const controller=new AbortController();controller.abort();await assert.rejects(installUploadedRoute(session,fixture(),author,{signal:controller.signal}),{name:'AbortError'});assert.equal(calls,0);
});
test('lost acknowledgement, incorrect hash or arbitrary path remains unconfirmed and never retries',async()=>{
 for(const response of [null,[new Uint8Array(16).fill(1),new Uint8Array(32),1024,installedRoutePath(fixture().sha256),1],[new Uint8Array(16).fill(1),fixture().sha256,1024,'/routes/../config',1]]){
  let calls=0;await assert.rejects(installUploadedRoute({async request(){calls++;if(response===null)throw Error('Disconnected');return response;}},fixture(),author),error=>error.code==='ROUTE_INSTALL_UNCONFIRMED'&&error.path===installedRoutePath(fixture().sha256));assert.equal(calls,1);
 }
});
test('explicit rejection stays rejected and mutable caller bytes cannot alter the expected receipt',async()=>{
 const error=Object.assign(Error('Busy'),{code:5});await assert.rejects(installUploadedRoute({async request(){throw error;}},fixture(),author),candidate=>candidate===error);
 const upload=fixture(),original=upload.sha256.slice();
 const result=await installUploadedRoute({async request(op,body){upload.sha256.fill(9);upload.id.fill(9);return [body[0],body[1],1024,installedRoutePath(original),1];}},upload,author);
 assert.deepEqual(result.sha256,original);assert.equal(result.id[0],1);
});
test('admission is pending, and confirmation queries never reinstall or confuse missing with installed',async()=>{
 const calls=[],upload=fixture();let state=0;
 const session={async request(op,body){calls.push(op);return [body[0],body[1],1024,installedRoutePath(body[1]),state];}};
 const pending=await installUploadedRoute(session,upload,author);assert.equal(pending.pending,true);assert.equal(pending.installed,false);
 assert.equal((await queryRouteInstallation(session,pending,author)).pending,true);
 state=1;assert.equal((await queryRouteInstallation(session,pending,author)).installed,true);
 state=2;const absent=await queryRouteInstallation(session,pending,author);assert.equal(absent.missing,true);assert.equal(absent.installed,false);
 assert.deepEqual(calls,[85,95,95,95]);
});
test('confirmation refuses a different connected author before sending any request',async()=>{
 const binding={...fixture(),authorKey:author.publicKey.slice()},different={...author,publicKey:new Uint8Array(64).fill(9)};let calls=0;
 await assert.rejects(queryRouteInstallation({async request(){calls++;}},binding,different),{code:'ROUTE_AUTHOR_CHANGED'});assert.equal(calls,0);
});
