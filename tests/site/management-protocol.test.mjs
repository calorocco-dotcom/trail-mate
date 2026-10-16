import test from 'node:test';
import assert from 'node:assert/strict';
import {encodeManagement,decodeManagement,ManagementSession,OPERATION} from '../../site/device/src/management-protocol.js';
import {FRAME,CAPABILITY} from '../../site/device/src/hostlink.js';
test('identity restore marks transport sensitive and clears its encoded payload after rejection',async()=>{
 let listener,retained;
 const connection={state:'ready',hello:{capabilities:CAPABILITY.MANAGEMENT},subscribe(fn){listener=fn;return()=>{}},async request(type,payload,options){retained=payload;assert.equal(options.sensitive,true);const decoded=decodeManagement(payload);listener({type:'frame',frame:{type:FRAME.MANAGEMENT_RESPONSE,payload:encodeManagement(decoded.session,decoded.request,5,[])}});}};
 const management=new ManagementSession(connection);await assert.rejects(management.request(OPERATION.IDENTITY_BACKUP_RESTORE,[new Uint8Array(96).fill(7)]),error=>error.code===5);assert.ok(retained.every(byte=>byte===0));management.close();
});
const session=new Uint8Array(16).fill(1),request=new Uint8Array(16).fill(2);

test('canonical validation clears its temporary material without mutating caller bytes or the wire frame',()=>{
 const secret=new Uint8Array(96).fill(7),original=secret.slice(),cleared=[],fill=Uint8Array.prototype.fill;
 let encoded;
 try{
  Uint8Array.prototype.fill=function(value,...rest){if(value===0)cleared.push({bytes:this,before:this.slice()});return fill.call(this,value,...rest);};
  encoded=encodeManagement(session,request,OPERATION.IDENTITY_BACKUP_RESTORE,[secret,true]);
 }finally{Uint8Array.prototype.fill=fill;}
 assert.deepEqual(secret,original);
 const copies=cleared.filter(item=>item.before.length===96&&item.before.every(value=>value===7));
 assert.equal(copies.length,1);assert.notEqual(copies[0].bytes,secret);assert.ok(copies[0].bytes.every(value=>value===0));
 const canonical=cleared.find(item=>item.before.length===encoded.length);assert.deepEqual(canonical.before,encoded);assert.ok(canonical.bytes.every(value=>value===0));
 const decoded=decodeManagement(encoded);assert.deepEqual(decoded.body[0],original);assert.equal(decoded.body[1],true);
 decoded.body[0].fill(0);encoded.fill(0);secret.fill(0);original.fill(0);
});
test('canonical envelope wire vector and maximum frame budget',()=>{
 const encoded=encodeManagement(session,request,OPERATION.CAPABILITIES,[]);
 assert.deepEqual([...encoded],[0x95,1,0xc4,16,...session,0xc4,16,...request,0,0x90]);
 assert.deepEqual(decodeManagement(encoded).body,[]);
 assert.throws(()=>encodeManagement(session,request,0,[new Uint8Array(512)]),e=>e.code==='LIMIT_EXCEEDED');
 assert.throws(()=>encodeManagement(new Uint8Array(15),request,0,[]));
 assert.throws(()=>decodeManagement(new Uint8Array([...encoded,0])));
});
test('legacy firmware cannot advertise an author management session',()=>{
 assert.throws(()=>new ManagementSession({hello:{capabilities:2}}),e=>e.code==='UNSUPPORTED');
});
test('responses bind both session and request, not merely serial sequence',async()=>{
 let listener;const fake={state:'ready',hello:{capabilities:CAPABILITY.MANAGEMENT},
 subscribe(fn){listener=fn;return()=>{}},async request(type,payload){assert.equal(type,FRAME.MANAGEMENT_REQUEST);const r=decodeManagement(payload);
 listener({type:'frame',frame:{type:FRAME.MANAGEMENT_RESPONSE,payload:encodeManagement(new Uint8Array(16),r.request,0,['wrong session'])}});
 listener({type:'frame',frame:{type:FRAME.MANAGEMENT_RESPONSE,payload:encodeManagement(r.session,r.request,0,['correct'])}});}};
 const m=new ManagementSession(fake,{sessionId:session});assert.deepEqual(await m.request(0,[],{requestId:request}),['correct']);m.close();
 await assert.rejects(m.request(0,[]),e=>e.code==='DISCONNECTED');
});
test('device rejection is preserved and disconnect clears pending requests',async()=>{
 let listener;const fake={state:'ready',hello:{capabilities:CAPABILITY.MANAGEMENT},subscribe(fn){listener=fn;return()=>{}},async request(type,payload){const r=decodeManagement(payload);listener({type:'frame',frame:{type:FRAME.MANAGEMENT_RESPONSE,payload:encodeManagement(r.session,r.request,4,[])}});}};
 const m=new ManagementSession(fake,{sessionId:session});await assert.rejects(m.request(0,[],{requestId:request}),e=>e.code===4);m.close();
 const silent={...fake,async request(){}};const n=new ManagementSession(silent,{sessionId:session});const p=n.request(0,[],{requestId:request});listener({type:'state',state:'disconnected'});await assert.rejects(p,e=>e.code==='DISCONNECTED');
});
