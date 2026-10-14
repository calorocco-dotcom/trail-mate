import test from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {saveConfigDocument} from '../../site/device/src/config-save.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';
const id=new Uint8Array(16).fill(1),baseHash=new Uint8Array(32).fill(2);
const document=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Old\nEND\n'));
const digest=async raw=>new Uint8Array(await webcrypto.subtle.digest('SHA-256',raw));
test('save uploads exact working document before committing against base hash',async()=>{
 let total=0,offset=0,hash;const calls=[];
 const session={async request(op,body){calls.push(op);if(op===OPERATION.TRANSFER_BEGIN){total=body[2];hash=body[3];}if(op===OPERATION.TRANSFER_CHUNK)offset+=body[2].length;if(op===OPERATION.CONFIG_COMMIT){assert.equal(offset,total);assert.deepEqual(body,[id,baseHash]);return [id,hash,1];}return [id,320,offset,op===OPERATION.TRANSFER_FINISH?1:0];}};
 const result=await saveConfigDocument(session,document,baseHash,{id,digest});assert.equal(result.restartPending,true);assert.equal(calls.at(-1),OPERATION.CONFIG_COMMIT);
});
test('a storage error prevents commit, and wrong receipt cannot mean saved',async()=>{
 const calls=[];await assert.rejects(saveConfigDocument({async request(op){calls.push(op);throw Error('Storage unavailable');}},document,baseHash,{id,digest}),/Storage/);assert.ok(!calls.includes(OPERATION.CONFIG_COMMIT));
});

test('incomplete Wi-Fi collection never uploads or commits',async()=>{
 const invalid=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nwifi.profile_count=u8:1\nwifi.profile.0.ssid=str:\nwifi.profile.0.password=str:\nEND\n'));
 let calls=0;await assert.rejects(saveConfigDocument({async request(){calls++;}},invalid,baseHash,{id,digest}),/SSID/);assert.equal(calls,0);
});

test('readback checkpoint runs after upload verification and before commit; failure prevents commit',async()=>{
 for(const reject of [false,true]){
  let offset=0,hash,persisted=false;const calls=[];
  const session={async request(op,body){calls.push(op);if(op===OPERATION.TRANSFER_BEGIN)hash=body[3];if(op===OPERATION.TRANSFER_CHUNK)offset+=body[2].length;if(op===OPERATION.CONFIG_COMMIT){assert.equal(persisted,true);return [id,hash,1];}return [id,320,offset,op===OPERATION.TRANSFER_FINISH?1:0];}};
  const saving=saveConfigDocument(session,document,baseHash,{id,digest,beforeCommit:async receipt=>{
   assert.equal(calls.at(-1),OPERATION.TRANSFER_FINISH);assert.deepEqual(receipt.id,id);assert.deepEqual(receipt.sha256,hash);
   if(reject)throw Error('Checkpoint unavailable');persisted=true;
  }});
  if(reject){await assert.rejects(saving,/Checkpoint unavailable/);assert.ok(!calls.includes(OPERATION.CONFIG_COMMIT));}
  else await saving;
 }
});
