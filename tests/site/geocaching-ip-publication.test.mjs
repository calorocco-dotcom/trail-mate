import test from 'node:test';
import assert from 'node:assert/strict';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,verifySigned} from '../../site/geocaching/src/protocol.js';
import {publishSignedCache} from '../../site/geocaching/src/publish-signed.js';
async function fixture(){
 const identity=await Identity.generate(),author=await identity.getPublicKey();
 const raw=encode([1,author,new Uint8Array(16).fill(1),1,null,0,1,1,'Cache','','',2,2,0,1,1]);
 const domain=new TextEncoder().encode('trailmate.geocache/sign/v1\0'),message=new Uint8Array(domain.length+raw.length);message.set(domain);message.set(raw,domain.length);
 const signed=[raw,await identity.sign(message)],verified=await verifySigned(signed),s=verified.summary;
 const directory={ready:true},calls=[];
 const client={closed:false,directories:new Map([['directory',directory]]),enqueue:fn=>fn(),async request(target,op,body){calls.push({target,op,body});return [s[0],s[1],s[2],0,s[1],s[2],s[3]];}};
 return {client,author,signed,calls};
}
test('browser IP publication sends signed device content and validates directory receipt',async()=>{
 const f=await fixture(),result=await publishSignedCache(f.client,'directory',f.signed,f.author);
 assert.equal(result.confirmed,true);assert.equal(f.calls.length,1);assert.equal(f.calls[0].op,1);
 assert.notEqual(f.calls[0].body[0][0],f.signed[0]);assert.deepEqual(f.calls[0].body[0],f.signed);
});
test('wrong author or corrupt signature never reaches network',async()=>{
 const f=await fixture();await assert.rejects(publishSignedCache(f.client,'directory',f.signed,new Uint8Array(64)),/author/);
 f.signed[1][0]^=1;await assert.rejects(publishSignedCache(f.client,'directory',f.signed,f.author),/signature/i);assert.equal(f.calls.length,0);
});
test('changed directory is rejected before sending, lost or mismatched receipt is unconfirmed',async()=>{
 const f=await fixture();f.client.enqueue=fn=>{f.client.directories.set('directory',{ready:true});return fn();};
 await assert.rejects(publishSignedCache(f.client,'directory',f.signed,f.author),/changed/);assert.equal(f.calls.length,0);
 f.client.enqueue=fn=>fn();f.client.request=async()=>{throw Error('Disconnected');};
 await assert.rejects(publishSignedCache(f.client,'directory',f.signed,f.author),e=>e.code==='PUBLICATION_UNCONFIRMED');
 f.client.request=async()=>[];
 await assert.rejects(publishSignedCache(f.client,'directory',f.signed,f.author),e=>e.code==='PUBLICATION_UNCONFIRMED');
});
