import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync,execFileSync} from 'node:child_process';
import path from 'node:path';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest,verifyTrackSuccessor} from '../../site/tracks/src/manifest.js';
import {encodeTrackAttachmentIndex,decodeTrackAttachmentIndex,matchTrackAttachmentIndex} from '../../site/tracks/src/attachment-index.js';
const root=path.resolve('.codex-build/hostlink-codec/native'),suffix=process.platform==='win32'?'.exe':'';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
const identity=await Identity.generate(),author=await identity.getPublicKey(),hash=new Uint8Array(32).fill(3);
const hex=bytes=>Buffer.from(bytes).toString('hex');
const metadata=()=>[1,['hike','山径'],2,1,0,0,1,null];
const base=()=>[1,author.slice(),new Uint8Array(16).fill(2),0,1,null,0,100,100,'山径','说明',[0,0,0,0],[0,null,null,2],[hash.slice(),300,'application/vnd.google-earth.kml+xml',1]];
const entry=(id,reference,required=1)=>[id,new Uint8Array(32).fill(7),100,'image/jpeg',reference,0,required];
const index=()=>[1,hash.slice(),300,[entry('img-0001','images/山.jpg'),entry('img-0002','https://example.invalid/a?x=1&y=2',0)]];
const extended=async (raw,count=2)=>[2,...base().slice(1),metadata(),[await Identity.fullHash(raw),raw.length,count,1]];
const sign=async record=>{const raw=encodeTrackManifest(record);return [raw,await identity.sign(trackManifestSigningBytes(raw))];};
const signUnchecked=async record=>{const raw=encode(record);return [raw,await identity.sign(new Uint8Array([...new TextEncoder().encode('TrailMate.Track.Manifest.v'+record[0]+'\0'),...raw]))];};
const native=(mode,...rows)=>spawnSync(path.join(root,'track_manifest_extensions'+suffix),[],{input:[mode,...rows.map(hex)].join('\n')+'\n',encoding:'utf8',maxBuffer:1048576});
const py=(mode,...rows)=>spawnSync(python,['-m','tools.tracks.extension_interop'],{input:[mode,...rows.map(hex)].join('\n')+'\n',encoding:'utf8',maxBuffer:1048576});
test('browser v2 signature and both domain hashes agree with real C++ and Python RNS',async()=>{
 const bytes=encodeTrackAttachmentIndex(index()),signed=await sign(await extended(bytes)),proof=await verifyTrackManifest(signed);
 const cpp=spawnSync(path.join(root,'track_manifest_verify'+suffix),[hex(encode(signed))],{encoding:'utf8'}),pythonResult=py('VERIFY',...signed);
 for(const result of [cpp,pythonResult]){assert.equal(result.status,0,result.stderr);assert.deepEqual(result.stdout.trim().split(/\r?\n/),[hex(proof.objectId),hex(proof.revisionHash)]);}
 const wrong=await identity.sign(new Uint8Array([...new TextEncoder().encode('TrailMate.Track.Manifest.v1\0'),...signed[0]]));
 await assert.rejects(verifyTrackManifest([signed[0],wrong]));assert.equal(py('VERIFY',signed[0],wrong).status,1);
});
test('native v2 signing primitive is accepted by browser and Python without changing v1 vector',async()=>{
 for(const version of ['1','2']){
  const lines=execFileSync(path.join(root,'track_manifest_sign'+suffix),version==='2'?['2']:[],{encoding:'utf8'}).trim().split(/\r?\n/),signed=lines.map(value=>new Uint8Array(Buffer.from(value,'hex'))),proof=await verifyTrackManifest(signed);
  assert.equal(proof.record[0],Number(version));assert.equal(py('VERIFY',...signed).status,0);
 }
});
test('signed index binding, raw KML identity and required count agree across all three codecs',async()=>{
 const bytes=encodeTrackAttachmentIndex(index()),signed=await sign(await extended(bytes));
 const matched=await matchTrackAttachmentIndex(bytes,signed[0]);assert.deepEqual(matched.record,index());
 const expected=`2,1,${300+bytes.length+200}`;
 for(const result of [native('INDEX',bytes,signed[0]),py('INDEX',bytes,...signed)]){assert.equal(result.status,0,result.stderr);assert.equal(result.stdout.trim(),expected);}
 const content=bytes.slice(),manifest=signed[0].slice(),pending=matchTrackAttachmentIndex(content,manifest);content.fill(0);manifest.fill(0);assert.deepEqual((await pending).record,index());
 const changed=bytes.slice();changed[changed.length-1]^=1;await assert.rejects(matchTrackAttachmentIndex(changed,signed[0]));assert.equal(native('INDEX',changed,signed[0]).status,1);assert.equal(py('INDEX',changed,...signed).status,1);
});
test('v1 to v2 retains object ID, v2 downgrade and archive attachment mutation are rejected',async()=>{
 const first=await sign(base()),previous=await verifyTrackManifest(first),bytes=encodeTrackAttachmentIndex(index()),record=await extended(bytes);
 record[4]=2;record[5]=previous.revisionHash;record[8]=101;const second=await sign(record),next=await verifyTrackManifest(second);
 assert.deepEqual(next.objectId,previous.objectId);assert.equal(verifyTrackSuccessor(previous,next),true);
 assert.equal(native('CHAIN',first[0],second[0],previous.revisionHash).status,0);assert.equal(py('CHAIN',...first,...second).status,0);
 const archive=decode(second[0],4096);archive[4]++;archive[5]=next.revisionHash;archive[6]=1;archive[8]++;
 const third=await sign(archive);assert.equal(verifyTrackSuccessor(next,await verifyTrackManifest(third)),true);
 assert.equal(native('CHAIN',second[0],third[0],next.revisionHash).status,0);assert.equal(py('CHAIN',...second,...third).status,0);
 archive[15][0][0]^=1;const altered=await sign(archive),alteredProof=await verifyTrackManifest(altered);assert.throws(()=>verifyTrackSuccessor(next,alteredProof),/原附件/);
 assert.equal(native('CHAIN',second[0],altered[0],next.revisionHash).status,1);assert.equal(py('CHAIN',...second,...altered).status,1);
 archive[15]=null;const dropped=await sign(archive),droppedProof=await verifyTrackManifest(dropped);assert.throws(()=>verifyTrackSuccessor(next,droppedProof),/原附件/);
 assert.equal(native('CHAIN',second[0],dropped[0],next.revisionHash).status,1);assert.equal(py('CHAIN',...second,...dropped).status,1);
 const downgraded=base();downgraded[4]=3;downgraded[5]=next.revisionHash;downgraded[8]=102;const low=await sign(downgraded),lowProof=await verifyTrackManifest(low);
 assert.throws(()=>verifyTrackSuccessor(next,lowProof),/版本链/);assert.equal(native('CHAIN',second[0],low[0],next.revisionHash).status,1);assert.equal(py('CHAIN',...second,...low).status,1);
});
test('all codecs reject malformed attachments, aliases, unsafe paths, MIME and package budgets',async()=>{
 const changes=[r=>r[3][0][0]='../x',r=>r[3][1][0]='IMG-0001',r=>r[3][1][4]=r[3][0][4],r=>r[3][0][1]=new Uint8Array(31),r=>r[3][0][2]=0,r=>r[3][0][2]=4194305,
  r=>r[3][0][3]='image/png;foo',r=>r[3][0][3]='image',r=>r[3][0][5]=4,r=>r[3][0][6]=true,r=>r[3][0][6]=2,
  ...['../a','a/%2e%2e/b','/absolute','a%2fb','a\\b','file://a','javascript:a','data:image/png,a','//host/a','a%5cb','a%00b','a%3ab','a/%zz/b','http://user@host/a','http://','httpſ://host/a'].map(reference=>r=>r[3][0][4]=reference),
  r=>r[3]=Array.from({length:65},(_,i)=>entry('id'+i,'a'+i)),
  r=>r[3]=Array.from({length:8},(_,i)=>['id'+i,new Uint8Array(32),8388608,'application/octet-stream','a'+i,3,1])];
 for(const change of changes){
  const row=index();change(row);assert.throws(()=>encodeTrackAttachmentIndex(row));const bytes=encode(row),record=await extended(bytes,Math.min(64,row[3].length)),signed=await signUnchecked(record);
  await assert.rejects(matchTrackAttachmentIndex(bytes,signed[0]));assert.equal(native('INDEX',bytes,signed[0]).status,1);assert.equal(py('INDEX',bytes,...signed).status,1);
 }
 const bytes=encodeTrackAttachmentIndex(index());
 for(const malformed of [bytes.slice(0,-1),new Uint8Array([...bytes,0])]){
  const signed=await signUnchecked(await extended(malformed));await assert.rejects(matchTrackAttachmentIndex(malformed,signed[0]));assert.equal(native('INDEX',malformed,signed[0]).status,1);assert.equal(py('INDEX',malformed,...signed).status,1);
 }
});
test('all codecs reject metadata and descriptor semantic mismatches',async()=>{
 const bytes=encodeTrackAttachmentIndex(index());
 for(const change of [r=>r[14][1]=['same','same'],r=>r[14][0]=256,r=>r[14][3]=3,r=>r[14][4]=3,r=>r[14][5]=3,r=>r[14][6]=5,r=>r[14][7]=[new Uint8Array(31),new Uint8Array(32)],r=>r[15][3]=2,r=>r[15][2]=0,r=>r[15][1]=294913]){
  const row=await extended(bytes);change(row);assert.throws(()=>encodeTrackManifest(row));const signed=await signUnchecked(row);
  await assert.rejects(verifyTrackManifest(signed));assert.equal(native('INDEX',bytes,signed[0]).status,1);assert.equal(py('VERIFY',...signed).status,1);
 }
 for(const change of [r=>r[13][0][0]^=1,r=>r[13][1]++,r=>r[15][2]=1,r=>r[15][1]--]){
  const row=await extended(bytes);change(row);const signed=await sign(row);await assert.rejects(matchTrackAttachmentIndex(bytes,signed[0]));assert.equal(native('INDEX',bytes,signed[0]).status,1);assert.equal(py('INDEX',bytes,...signed).status,1);
 }
});
test('64 exact references at 4096 UTF-8 bytes fit the bounded index without expanding main manifest',async()=>{
 const row=index();row[3]=Array.from({length:64},(_,i)=>entry('id'+i,('images/'+i+'/').padEnd(4096,'x')));
 const bytes=encodeTrackAttachmentIndex(row);assert.ok(bytes.length<294912&&bytes.length>262144);
 const signed=await sign(await extended(bytes,64));assert.ok(signed[0].length<4096);assert.equal((await matchTrackAttachmentIndex(bytes,signed[0])).record[3].length,64);
 const expected=`64,64,${row[2]+bytes.length+6400}`;
 for(const result of [native('INDEX',bytes,signed[0]),py('INDEX',bytes,...signed)]){assert.equal(result.status,0,result.stderr);assert.equal(result.stdout.trim(),expected);}
});
