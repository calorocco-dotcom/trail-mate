import test from 'node:test';import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';
import {zipSync,unzipSync} from '../../site/node_modules/fflate/esm/index.mjs';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {exportDraftBackup,readDraftBackup,validateDraftBackupSnapshot} from '../../site/tracks/src/draft-backup.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes} from '../../site/tracks/src/manifest.js';
const encoder=new TextEncoder(),source=encoder.encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>');
const rows=[{id:'photo',reference:'photo.png',mime:'image/png',role:0,required:true,content:new Uint8Array([1,2,3])}];
const fixture=()=>({source,name:'测试备份',filename:'track.kml',attachmentRows:rows,editorMetadata:{activity:255,tags:['山脊','loop']},originalSource:source.slice(),archiveMember:'nested/original.kml'});
test('draft backup preserves exact files, aliases, metadata and original without jobs or credentials',async()=>{
 const input={...fixture(),jobBinding:new Uint8Array([9]),publicationPlan:new Uint8Array([8]),secretKey:'PRIVATE_TEST_SECRET',attachmentBindings:new Uint8Array([7])};input.attachmentRows=[...rows,{...rows[0],id:'alias',reference:'alias.png'}];
 const value=await exportDraftBackup(input),restored=await readDraftBackup(value.bytes);assert.deepEqual(restored.source,input.source);assert.deepEqual(restored.originalSource,input.originalSource);assert.equal(restored.archiveMember,input.archiveMember);assert.deepEqual(restored.editorMetadata,input.editorMetadata);assert.equal(restored.attachmentRows.length,2);assert.ok(!JSON.stringify(value.info).includes('PRIVATE_TEST_SECRET'));for(const key of ['jobBinding','publicationPlan','secretKey','attachmentBindings'])assert.equal(key in restored,false);
 const files=unzipSync(value.bytes);assert.equal(Object.keys(files).filter(path=>path.startsWith('resources/')).length,1);assert.deepEqual(files['draft.kml'],source);
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??(process.platform==='win32'?'C:/Users/vicliu/scoop/apps/python313/current/python.exe':'python3');const data=execFileSync(python,['-c','import sys,io,zipfile,json\nz=zipfile.ZipFile(io.BytesIO(sys.stdin.buffer.read()));assert z.testzip() is None\nprint(json.dumps(json.loads(z.read("draft.json"))["format"]))'],{input:Buffer.from(value.bytes),windowsHide:true,encoding:'utf8'});assert.equal(JSON.parse(data),'trailmate.track.draft');
});
async function signedFixture(){const actor=await Identity.generate(),hash=await Identity.fullHash(source),record=[1,await actor.getPublicKey(),new Uint8Array(16).fill(1),0,1,null,0,100,100,'Parent','',[10000000,20000000,10000000,20000000],[0,null,null,1],[hash,source.length,'application/vnd.google-earth.kml+xml',1]],manifest=encodeTrackManifest(record);return encode([manifest,await actor.sign(trackManifestSigningBytes(manifest))]);}
test('verified public provenance and archive parent survive; invalid signature or changed archive bytes reject',async()=>{
 const proof=await signedFixture(),input={...fixture(),sourceProof:proof,predecessor:proof,archive:true};const restored=await readDraftBackup((await exportDraftBackup(input)).bytes);assert.deepEqual(restored.sourceProof,proof);assert.deepEqual(restored.predecessor,proof);assert.equal(restored.archive,true);
 const pair=decode(proof,4352);pair[1][0]^=1;await assert.rejects(exportDraftBackup({...input,sourceProof:encode(pair)}));await assert.rejects(exportDraftBackup({...input,source:new Uint8Array([1])}));await assert.rejects(exportDraftBackup({...fixture(),archive:true}));await assert.rejects(exportDraftBackup({...fixture(),attachmentTemplate:new Uint8Array([1])}));
});
test('corrupt contents, wrong inventory, extra members and format confusion reject',async()=>{
 const value=await exportDraftBackup(fixture()),files=unzipSync(value.bytes),pack=(changes)=>zipSync({...files,...changes},{level:6});
 const bad=source.slice();bad[0]^=1;await assert.rejects(readDraftBackup(pack({'draft.kml':bad})));await assert.rejects(readDraftBackup(pack({'original.kml':bad})));await assert.rejects(readDraftBackup(pack({'extra.txt':new Uint8Array([1])})));await assert.rejects(readDraftBackup(pack({'draft.json':encoder.encode('{}')})));
 const info=JSON.parse(new TextDecoder().decode(files['draft.json']));info.attachments[0][7]='../escape.bin';await assert.rejects(readDraftBackup(pack({'draft.json':encoder.encode(JSON.stringify(info))})));await assert.rejects(readDraftBackup(zipSync({'doc.kml':source})));await assert.rejects(readDraftBackup(value.bytes.slice(0,-1)));
});
test('empty attachments and absent originals remain absent; cancellation and malformed input reject',async()=>{
 const input={source,name:'Simple',filename:'simple.kml'},restored=await readDraftBackup((await exportDraftBackup(input)).bytes);assert.deepEqual(restored.attachmentRows,[]);assert.equal(restored.originalSource,undefined);assert.deepEqual(restored.editorMetadata,{activity:0,tags:[]});
 const controller=new AbortController();controller.abort();await assert.rejects(exportDraftBackup(input,{signal:controller.signal}),error=>error.name==='AbortError');await assert.rejects(readDraftBackup(new Uint8Array(),{signal:controller.signal}),error=>error.name==='AbortError');await assert.rejects(validateDraftBackupSnapshot({...input,name:''}));await assert.rejects(validateDraftBackupSnapshot({...input,source:new Uint8Array(8388609)}));
});
test('snapshot fixes all bytes and metadata before asynchronous attachment hashing',async()=>{
 const input=fixture(),pending=validateDraftBackupSnapshot(input),expected=input.originalSource.slice();
 input.originalSource[0]^=1;input.editorMetadata.tags.push('late');input.archiveMember='changed.kml';input.name='changed';
 const value=await pending;assert.deepEqual(value.originalSource,expected);assert.deepEqual(value.editorMetadata,{activity:255,tags:['山脊','loop']});assert.equal(value.archiveMember,'nested/original.kml');assert.equal(value.name,'测试备份');
});
