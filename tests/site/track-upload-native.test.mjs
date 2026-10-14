import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
const exe=path.resolve('.codex-build/hostlink-codec/native/upload_protocol'+(process.platform==='win32'?'.exe':''));
const id=new Uint8Array(16).fill(7),hash=new Uint8Array(32).fill(9);
function run(command,raw){const result=spawnSync(exe,[command,Buffer.from(raw).toString('hex')],{encoding:'utf8'});if(result.error)throw result.error;return result;}
test('browser CMP upload requests and native progress interoperate',()=>{
 for(const [command,record,summary,offset,sealed] of [
  ['begin',[id,3,8388608,hash],'8388608,0,0',0,0],
  ['begin',[id,4,4096,hash],'4096,0,0',0,0],
  ['begin',[id,7,1100,hash],'1100,0,0',0,0],
  ['begin',[id,8,294912,hash],'294912,0,0',0,0],
  ['begin',[id,9,8388608,hash],'8388608,0,0',0,0],
  ['begin',[id,10,1100,hash],'1100,0,0',0,0],
  ['begin',[id,11,4166,hash],'4166,0,0',0,0],
  ['chunk',[id,320,new Uint8Array(320).fill(3)],'0,320,320',640,0],
  ['finish',[id],'0,0,0',640,1]
 ]){
  const result=run(command,encode(record));assert.equal(result.status,0,result.stderr);
  const [actual,hex]=result.stdout.trim().split(/\r?\n/);assert.equal(actual,summary);
  const progress=decode(new Uint8Array(Buffer.from(hex,'hex')));assert.deepEqual(progress,[id,320,offset,sealed]);
 }
});
test('native upload rejects invalid browser input and framing tails',()=>{
 for(const [command,record] of [
  ['begin',[id,2,100,hash]],['begin',[id,3,8388609,hash]],['begin',[new Uint8Array(16),3,100,hash]],
  ['begin',[id,4,4097,hash]],['begin',[id,4,0,hash]],['begin',[id,5,100,hash]],
  ['begin',[id,7,1101,hash]],['begin',[id,7,0,hash]],
  ['begin',[id,8,294913,hash]],['begin',[id,8,0,hash]],['begin',[id,9,8388609,hash]],
  ['begin',[id,264,1,hash]],['begin',[id,265,1,hash]],
  ['begin',[id,10,1101,hash]],['begin',[id,11,4167,hash]],['begin',[id,267,1,hash]],
  ['chunk',[id,0,new Uint8Array(321)]],['chunk',[id,8388607,new Uint8Array(2)]],['chunk',[id,0,new Uint8Array()]],
  ['finish',[id,0]]
 ])assert.equal(run(command,encode(record)).status,1);
 const raw=encode([id,3,640,hash]);assert.equal(run('begin',raw.slice(0,-1)).status,1);assert.equal(run('begin',new Uint8Array([...raw,0])).status,1);
});
