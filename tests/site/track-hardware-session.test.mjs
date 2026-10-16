import test from 'node:test';
import assert from 'node:assert/strict';
import {TrackHardwareSession} from '../../tools/hostlink/track_hardware_session.mjs';
import {decodeManagement,encodeManagement} from '../../site/device/src/management-protocol.js';
import {FRAME} from '../../site/device/src/hostlink.js';
import fs from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const id=value=>new Uint8Array(16).fill(value);
const hash=value=>new Uint8Array(32).fill(value);
function fixture(rows=[]){
 const session=Object.create(TrackHardwareSession.prototype),calls=[];
 session.fingerprint='ab'.repeat(32);session.author={verified:true,fingerprint:session.fingerprint,generation:2};
 session.id=id(1);session.uploads=new Map();
 session.exchange=async(type,payload,response)=>{
  assert.equal(type,FRAME.MANAGEMENT_REQUEST);assert.equal(response,FRAME.MANAGEMENT_RESPONSE);
  const request=decodeManagement(payload),body=request.body;calls.push([request.code,body]);
  let result=[];
  if(request.code===38){
   const offset=body[1]===null?0:Number(body[1][1]);
   result=[0,offset+1<rows.length?String(offset+1):null,rows[offset]??null];
  }
  return encodeManagement(request.session,request.request,0,result);
 };
 return {session,calls};
}

test('physical harness retains complete upload tuples and sends exact cleanup bindings',async()=>{
 const {session,calls}=fixture(),upload=id(2),digest=hash(3);
 await session.request(32,[upload,8,12,digest]);
 upload.fill(9);digest.fill(9);
 const retained=session.uploads.get(Buffer.from(id(2)).toString('hex'));
 assert.deepEqual(retained,{id:id(2),kind:8,total:12,hash:hash(3)});
 await session.request(37,[retained.id,retained.kind,retained.total,retained.hash]);
 assert.deepEqual(calls[1],[37,[id(2),8,12,hash(3)]]);
});

test('retained-upload cleanup matches kind, size, digest and current identity generation',async()=>{
 const wanted=[id(3),8,20,hash(4),2],rows=[wanted,
  [id(4),8,20,hash(4),1],[id(5),8,21,hash(4),2],
  [id(6),8,20,hash(5),2],[id(7),9,20,hash(4),2]];
 const {session,calls}=fixture(rows);
 await session.cleanupExactStaging(8,20,hash(4));
 assert.equal(calls.filter(([op])=>op===38).length,rows.length);
 assert.deepEqual(calls.filter(([op])=>op===37),[[37,wanted.slice(0,4)]]);
});

test('hardware harness rejects unrelated uploads and configuration writes before transport',async()=>{
 const {session,calls}=fixture();
 await assert.rejects(session.request(37,[id(9),8,20,hash(4)]),/Unrelated upload/);
 await assert.rejects(session.request(21,[]),/Unrelated device operation/);
 session.author={...session.author,fingerprint:'cd'.repeat(32)};
 await assert.rejects(session.request(38,[8,null]));
 assert.equal(calls.length,0);
});

test('physical attachment IDs survive reopening before a failed publication is retried',async t=>{
 const root=fileURLToPath(new URL('../../.codex-build/hostlink-codec/',import.meta.url));
 await fs.mkdir(root,{recursive:true});const folder=await fs.mkdtemp(path.join(root,'attachment-bindings-'));
 const checkpoint=path.join(folder,'device-confirmation.json'),file=checkpoint+'.attachments.json';
 t.after(async()=>{await fs.unlink(file).catch(error=>{if(error.code!=='ENOENT')throw error;});await fs.rmdir(folder);});
 const {session}=fixture();session.checkpointPath=checkpoint;
 const manifest=new Uint8Array([1,2,3]),first=await session.attachmentBindings(manifest);
 const saved=await fs.readFile(file,'utf8');
 const reopened=fixture().session;reopened.checkpointPath=checkpoint;
 assert.deepEqual(await reopened.attachmentBindings(manifest),first);
 assert.equal(await fs.readFile(file,'utf8'),saved,'Retry does not replace its retained random IDs');
 const next=await reopened.attachmentBindings(new Uint8Array([1,2,4]));
 assert.notDeepEqual(next,first);assert.deepEqual(await reopened.attachmentBindings(manifest),first);
 reopened.author={...reopened.author,generation:3};
 const unchanged=await fs.readFile(file,'utf8');
 await assert.rejects(reopened.attachmentBindings(manifest),/different identity/);
 assert.equal(await fs.readFile(file,'utf8'),unchanged);
});

test('physical upload errors identify the failing operation and upload kind',async()=>{
 const {session}=fixture();
 session.exchange=async(type,payload)=>{const request=decodeManagement(payload);return encodeManagement(request.session,request.request,7,[]);};
 await assert.rejects(session.request(32,[id(2),10,38,hash(3)]),error=>error.code===7&&error.operation===32&&error.uploadKind===10);
});
