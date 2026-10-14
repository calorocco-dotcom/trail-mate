import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {zipSync} from '../../site/node_modules/fflate/esm/index.mjs';
import {readKmlArchive,chooseArchiveKml,KML_ARCHIVE_LIMIT} from '../../site/tracks/src/kml-archive.js';

const utf8=new TextEncoder(),kml=utf8.encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Document><name>测试轨迹</name><Placemark><description><![CDATA[<img src="images/photo.png">]]></description><LineString><coordinates>1,2,3 4,5,6</coordinates></LineString></Placemark></Document></kml>');
const photo=Uint8Array.from({length:1024},(_,i)=>i%255);
function fixture(level=6){return zipSync({'doc.kml':kml,'images/photo.png':photo},{level,mtime:new Date(1980,0,1)});}
function central(bytes){const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.length);for(let i=0;i<bytes.length-46;i++)if(view.getUint32(i,true)===0x02014b50)return i;throw Error('Fixture lacks central directory');}
function edit(bytes,work){const next=bytes.slice();work(new DataView(next.buffer),central(next),next.length-22);return next;}
const code=expected=>error=>error.code===expected;

test('stored and Deflate KMZ preserve exact KML and resource bytes',async()=>{
 for(const level of [0,6,9]){const restored=await readKmlArchive(fixture(level));assert.equal(restored.preferred,'doc.kml');assert.deepEqual(restored.kmlFiles,['doc.kml']);assert.deepEqual(restored.files.get('images/photo.png'),photo);assert.deepEqual(chooseArchiveKml(restored).kml,kml);assert.equal(restored.totalBytes,kml.length+photo.length);}
});
test('multiple KML members require selection unless root doc.kml exists',async()=>{
 const archive=await readKmlArchive(zipSync({'routes/first.kml':kml,'routes/second.kml':kml},{level:6}));assert.equal(archive.preferred,null);assert.throws(()=>chooseArchiveKml(archive),code('KML_ARCHIVE_SELECT'));assert.deepEqual(chooseArchiveKml(archive,'routes/second.kml').kml,kml);assert.throws(()=>chooseArchiveKml(archive,'other.kml'),code('KML_ARCHIVE_SELECT'));
 const preferred=await readKmlArchive(zipSync({'DOC.KML':kml,'other.kml':kml}));assert.equal(preferred.preferred,'DOC.KML');
 const selected=chooseArchiveKml(preferred);selected.kml[0]^=1;assert.deepEqual(preferred.files.get('DOC.KML'),kml);
});
test('all legacy CP437 filename bytes agree with independent Python decoding',async()=>{
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??(process.platform==='win32'?'C:/Users/vicliu/scoop/apps/python313/current/python.exe':'python3');
 const expected=JSON.parse(execFileSync(python,['-c','import json;print(json.dumps([bytes([i]).decode("cp437") for i in range(128,256)]))'],{encoding:'utf8',windowsHide:true}));
 for(let i=0;i<128;i++){const bytes=edit(zipSync({'a.kml':kml},{level:0}),(v,c)=>{v.setUint8(30,i+128);v.setUint8(c+46,i+128);});const archive=await readKmlArchive(bytes);assert.equal(archive.preferred,expected[i]+'.kml');}
});
test('Unicode Path extra field resolves a legacy filename using its raw-name CRC',async()=>{
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??(process.platform==='win32'?'C:/Users/vicliu/scoop/apps/python313/current/python.exe':'python3');
 const script='import sys,io,zipfile,struct,zlib\no=io.BytesIO();i=zipfile.ZipInfo("legacy.kml");u=bytes([1])+struct.pack("<I",zlib.crc32(b"legacy.kml"))+"路线.kml".encode("utf8");i.extra=struct.pack("<HH",0x7075,len(u))+u\nwith zipfile.ZipFile(o,"w") as z:z.writestr(i,sys.stdin.buffer.read())\nsys.stdout.buffer.write(o.getvalue())';
 const bytes=execFileSync(python,['-c',script],{input:Buffer.from(kml),windowsHide:true}),archive=await readKmlArchive(new Uint8Array(bytes));assert.equal(archive.preferred,'路线.kml');assert.deepEqual(chooseArchiveKml(archive).kml,kml);
});
test('bounded ZIP64 central-directory metadata is supported and oversized values reject',async()=>{
 const zip=fixture(),end=zip.length-22,original=new DataView(zip.buffer),bytes=new Uint8Array(end+56+20+22),view=new DataView(bytes.buffer);bytes.set(zip.subarray(0,end));
 view.setUint32(end,0x06064b50,true);view.setBigUint64(end+4,44n,true);view.setUint16(end+12,45,true);view.setUint16(end+14,45,true);view.setBigUint64(end+24,BigInt(original.getUint16(end+8,true)),true);view.setBigUint64(end+32,BigInt(original.getUint16(end+10,true)),true);view.setBigUint64(end+40,BigInt(original.getUint32(end+12,true)),true);view.setBigUint64(end+48,BigInt(original.getUint32(end+16,true)),true);
 view.setUint32(end+56,0x07064b50,true);view.setBigUint64(end+64,BigInt(end),true);view.setUint32(end+72,1,true);bytes.set(zip.subarray(end),end+76);view.setUint16(end+84,0xffff,true);view.setUint16(end+86,0xffff,true);view.setUint32(end+88,0xffffffff,true);view.setUint32(end+92,0xffffffff,true);
 assert.deepEqual(chooseArchiveKml(await readKmlArchive(bytes)).kml,kml);view.setBigUint64(end+32,BigInt(Number.MAX_SAFE_INTEGER)+1n,true);view.setBigUint64(end+24,BigInt(Number.MAX_SAFE_INTEGER)+1n,true);await assert.rejects(readKmlArchive(bytes),code('KML_ARCHIVE_LIMIT'));
});
test('Unicode nested members, directories and comments are retained',async()=>{
 const zip=zipSync({'路线/':new Uint8Array(),'路线/步道.kml':kml,'照片/现场.png':photo},{level:6,comment:'KMZ fixture comment'}),archive=await readKmlArchive(zip);
 assert.equal(archive.preferred,'路线/步道.kml');assert.equal(archive.files.size,2);assert.deepEqual(archive.files.get('照片/现场.png'),photo);
});
test('independent Python zipfile ordinary and streamed ZIP64 archives are readable',async()=>{
 const python=process.env.TRAIL_MATE_TRACK_PYTHON??(process.platform==='win32'?'C:/Users/vicliu/scoop/apps/python313/current/python.exe':'python3');
 const script='import sys,io,zipfile\nclass Stream(io.BytesIO):\n def seek(self,*args): raise io.UnsupportedOperation("not seekable")\nstreamed=sys.argv[1]=="stream"\nout=Stream() if streamed else io.BytesIO()\nwith zipfile.ZipFile(out,"w",compression=zipfile.ZIP_DEFLATED) as z:\n with z.open("doc.kml","w",force_zip64=True) as f: f.write(sys.stdin.buffer.read())\n z.writestr("资源/图.png",b"resource")\nsys.stdout.buffer.write(out.getvalue())';
 for(const mode of ['ordinary','stream']){const bytes=execFileSync(python,['-c',script,mode],{input:Buffer.from(kml),windowsHide:true}),archive=await readKmlArchive(new Uint8Array(bytes));assert.deepEqual(chooseArchiveKml(archive).kml,kml);assert.equal(new TextDecoder().decode(archive.files.get('资源/图.png')),'resource');}
});
test('case and Unicode distinctions remain exact; duplicate paths and file-as-directory reject',async()=>{
 for(const files of [{'doc.kml':kml,'DOC.KML':kml},{'é.kml':kml,'e\u0301.kml':kml},{'a':photo,'A/doc.kml':kml}]){const archive=await readKmlArchive(zipSync(files));assert.deepEqual([...archive.files.keys()],Object.keys(files));}
 const ambiguous=await readKmlArchive(zipSync({'DOC.KML':kml,'Doc.kml':kml}));assert.equal(ambiguous.preferred,null);
 await assert.rejects(readKmlArchive(zipSync({'a':photo,'a/doc.kml':kml})),code('KML_ARCHIVE_PATH'));
 const duplicate=zipSync({'doc.kml':kml,'bad.kml':kml},{level:0}),view=new DataView(duplicate.buffer),first=central(duplicate),second=first+46+view.getUint16(first+28,true)+view.getUint16(first+30,true)+view.getUint16(first+32,true),local=view.getUint32(second+42,true);
 duplicate.set(utf8.encode('doc.kml'),second+46);duplicate.set(utf8.encode('doc.kml'),local+30);await assert.rejects(readKmlArchive(duplicate),code('KML_ARCHIVE_PATH'));
});
test('path traversal, absolute paths and backslashes reject',async()=>{
 for(const name of ['../doc.kml','/doc.kml','a/../doc.kml','a//doc.kml','C:/doc.kml','a\\doc.kml'])await assert.rejects(readKmlArchive(zipSync({[name]:kml})),code('KML_ARCHIVE_PATH'));
});
test('CRC, compressed content, local/central disagreement and missing end record reject',async()=>{
 const zip=fixture(0);
 await assert.rejects(readKmlArchive(edit(zip,(v,c)=>{const data=30+v.getUint16(26,true)+v.getUint16(28,true);v.setUint8(data,v.getUint8(data)^1);})),code('KML_ARCHIVE_INVALID'));
 await assert.rejects(readKmlArchive(edit(zip,(v,c)=>v.setUint32(c+16,123,true))),code('KML_ARCHIVE_INVALID'));
 await assert.rejects(readKmlArchive(edit(zip,v=>v.setUint8(30,65))),code('KML_ARCHIVE_INVALID'));
 await assert.rejects(readKmlArchive(zip.slice(0,-10)));
 const compressed=fixture(6);await assert.rejects(readKmlArchive(edit(compressed,v=>{const start=30+v.getUint16(26,true)+v.getUint16(28,true);v.setUint8(start,255);})),code('KML_ARCHIVE_INVALID'));
});
test('encryption, unsupported compression, split archives and symlinks reject explicitly',async()=>{
 const zip=fixture();
 await assert.rejects(readKmlArchive(edit(zip,(v,c)=>{v.setUint16(6,v.getUint16(6,true)|1,true);v.setUint16(c+8,v.getUint16(c+8,true)|1,true);})),code('KML_ARCHIVE_UNSUPPORTED'));
 await assert.rejects(readKmlArchive(edit(zip,(v,c)=>{v.setUint16(8,12,true);v.setUint16(c+10,12,true);})),code('KML_ARCHIVE_UNSUPPORTED'));
 await assert.rejects(readKmlArchive(edit(zip,(v,c,e)=>v.setUint16(e+4,1,true))));
 await assert.rejects(readKmlArchive(edit(zip,(v,c)=>v.setUint32(c+38,0xa0000000,true))),code('KML_ARCHIVE_UNSUPPORTED'));
});
test('admission limits precede extraction and forged Deflate output size cannot bypass the bound',async()=>{
 const zip=fixture();
 await assert.rejects(readKmlArchive(edit(zip,(v,c)=>{v.setUint32(22,KML_ARCHIVE_LIMIT.file+1,true);v.setUint32(c+24,KML_ARCHIVE_LIMIT.file+1,true);})),code('KML_ARCHIVE_LIMIT'));
 await assert.rejects(readKmlArchive(edit(zip,(v,c,e)=>{v.setUint16(e+8,257,true);v.setUint16(e+10,257,true);})),code('KML_ARCHIVE_LIMIT'));
 const bomb=zipSync({'doc.kml':new Uint8Array(1024*1024).fill(65)},{level:9});await assert.rejects(readKmlArchive(edit(bomb,(v,c)=>{v.setUint32(22,1,true);v.setUint32(c+24,1,true);})),code('KML_ARCHIVE_INVALID'));
 await assert.rejects(readKmlArchive(new Uint8Array(KML_ARCHIVE_LIMIT.input+1)),code('KML_ARCHIVE_LIMIT'));
});
test('cancellation before and during extraction stops without returning a partial archive',async()=>{
 const controller=new AbortController();controller.abort();await assert.rejects(readKmlArchive(fixture(),{signal:controller.signal}),error=>error.name==='AbortError');
 const running=new AbortController(),bytes=zipSync({'doc.kml':kml,'bulk.bin':new Uint8Array(2*1024*1024).fill(7)},{level:0});setTimeout(()=>running.abort(),0);await assert.rejects(readKmlArchive(bytes,{signal:running.signal}),error=>error.name==='AbortError');
 await assert.rejects(readKmlArchive(zipSync({'image.png':photo})),code('KML_ARCHIVE_NO_KML'));
});
