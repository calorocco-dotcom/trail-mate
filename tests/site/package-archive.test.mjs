import test from 'node:test';import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {zipSync,unzipSync} from '../../site/node_modules/fflate/esm/index.mjs';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes} from '../../site/tracks/src/manifest.js';
import {exportTrackPackageArchive,readTrackPackageArchive} from '../../site/tracks/src/package-archive.js';
async function fixture({version=2,assets=true,alias=false}={}){
 const actor=await Identity.generate(),kml=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>'),photo=new Uint8Array([1,2,3]),hash=await Identity.fullHash(kml),photoHash=await Identity.fullHash(photo),optionalHash=alias?photoHash:await Identity.fullHash(new Uint8Array([9,8,7]));
 const index=assets?encode([1,hash,kml.length,[['photo',photoHash,3,'image/png','images/photo.png',0,1],['optional',optionalHash,3,'image/png','images/optional.png',0,0]]]):null;
 const record=[version,await actor.getPublicKey(),new Uint8Array(16).fill(1),0,1,null,0,100,100,'Archive fixture','',[10000000,20000000,10000000,20000000],[0,null,null,1],[hash,kml.length,'application/vnd.google-earth.kml+xml',1]];
 if(version===2)record.push([0,[],0,0,1,0,0,null],index?[await Identity.fullHash(index),index.length,2,1]:null);
 const manifest=encodeTrackManifest(record);return {signed:[manifest,await actor.sign(trackManifestSigningBytes(manifest))],kml,attachmentIndex:index,attachmentFiles:index?[{id:'photo',content:photo}]:[]};
}
test('signed ZIP preserves exact original bytes and is readable with independent Python zipfile',async()=>{
 const input=await fixture(),value=await exportTrackPackageArchive(input),restored=await readTrackPackageArchive(value.bytes);assert.equal(value.complete,false);assert.equal(restored.complete,false);assert.deepEqual(restored.payload,input);assert.match(value.filename,/^tmtrack_[a-f0-9]{64}_[a-f0-9]{64}\.zip$/);
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??(process.platform==='win32'?path.resolve('.codex-build/reticulum-directory/venv/Scripts/python.exe'):'python3');
 const raw=execFileSync(python,['-c','import sys,io,zipfile,json,base64\nz=zipfile.ZipFile(io.BytesIO(sys.stdin.buffer.read()))\nassert z.testzip() is None\nassert all(i.compress_type==0 for i in z.infolist())\nprint(json.dumps({n:base64.b64encode(z.read(n)).decode() for n in z.namelist()}))'],{input:Buffer.from(value.bytes),maxBuffer:2*1024*1024});
 const files=JSON.parse(raw);assert.deepEqual(Buffer.from(files['track.kml'],'base64'),Buffer.from(input.kml));assert.deepEqual(decode(new Uint8Array(Buffer.from(files['signed-manifest.cmp'],'base64')),4166),input.signed);assert.deepEqual(Buffer.from(files['attachment-index.cmp'],'base64'),Buffer.from(input.attachmentIndex));assert.equal(Object.keys(files).filter(name=>name.startsWith('resources/')).length,1);
});
test('v1 and v2 nil-resource packages round trip without inventing an index',async()=>{
 for(const version of [1,2]){const input=await fixture({version,assets:false}),value=await exportTrackPackageArchive(input),restored=await readTrackPackageArchive(value.bytes);assert.equal(value.complete,true);assert.deepEqual(restored.payload,input);assert.equal(Object.hasOwn(unzipSync(value.bytes),'attachment-index.cmp'),false);}
});
test('same hash aliases occupy one ZIP file and restore each signed resource ID',async()=>{
 const input=await fixture({alias:true}),value=await exportTrackPackageArchive(input),restored=await readTrackPackageArchive(value.bytes);assert.equal(value.complete,true);assert.equal(restored.payload.attachmentFiles.length,2);assert.deepEqual(restored.payload.attachmentFiles.map(row=>row.id),['photo','optional']);assert.equal(Object.keys(unzipSync(value.bytes)).filter(name=>name.startsWith('resources/')).length,1);
});
test('bad signed bytes, missing mandatory files, cancellation and invalid export input reject',async()=>{
 const input=await fixture(),bad=input.kml.slice();bad[0]^=1;await assert.rejects(exportTrackPackageArchive({...input,kml:bad}));const signature=input.signed[1].slice();signature[0]^=1;await assert.rejects(exportTrackPackageArchive({...input,signed:[input.signed[0],signature]}));await assert.rejects(exportTrackPackageArchive({...input,attachmentFiles:[]}));const controller=new AbortController();controller.abort();await assert.rejects(exportTrackPackageArchive(input,{signal:controller.signal}),error=>error.name==='AbortError');
});
test('import rejects traversal, unrelated resources, bad inventory, compressed entries and content tampering',async()=>{
 const input=await fixture(),value=await exportTrackPackageArchive(input),files=unzipSync(value.bytes),repack=changes=>zipSync({...files,...changes},{level:0});
 await assert.rejects(readTrackPackageArchive(repack({'../evil':new Uint8Array([1])})));await assert.rejects(readTrackPackageArchive(repack({['resources/'+'f'.repeat(64)+'.bin']:new Uint8Array([1])})));await assert.rejects(readTrackPackageArchive(repack({'package.json':new TextEncoder().encode('{}')})));await assert.rejects(readTrackPackageArchive(zipSync(files,{level:6})));const bad=input.kml.slice();bad[0]^=1;await assert.rejects(readTrackPackageArchive(repack({'track.kml':bad})));await assert.rejects(readTrackPackageArchive(value.bytes.slice(0,-10)));
});
