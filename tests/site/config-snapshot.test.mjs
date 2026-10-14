import test from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {readConfigSnapshot} from '../../site/device/src/config-snapshot.js';
const data=new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:TrailMate\nEND\n');
const digest=async b=>new Uint8Array(await webcrypto.subtle.digest('SHA-256',b));
test('snapshot binds content hash across chunks and validates final file',async()=>{
 const hash=await digest(data);const progress=[];
 const session={async request(op,[offset,count,expected]){assert.equal(op,17);if(offset)assert.deepEqual(expected,hash);return [offset,data.length,hash,data.slice(offset,offset+25)];}};
 const result=await readConfigSnapshot(session,{digest,onProgress:p=>progress.push(p)});
 assert.equal(result.document.get('device.node_name').value,'TrailMate');assert.equal(progress.at(-1).offset,data.length);
});
test('changed generation and corrupted contents cannot become an editable snapshot',async()=>{
 const hash=await digest(data);let calls=0;
 await assert.rejects(readConfigSnapshot({async request(op,[offset]){const h=hash.slice();if(calls++)h[0]^=1;return [offset,data.length,h,data.slice(offset,offset+25)];}},{digest}),/changed/);
 await assert.rejects(readConfigSnapshot({async request(){const b=data.slice();b[4]^=1;return [0,data.length,hash,b];}},{digest}),/hash mismatch/);
});
