import {zipSync,unzipSync} from 'fflate';
import {encode,decode,equal} from '../../geocaching/src/protocol.js';
import {verifyInstallPayload,packageInstallHex} from './package-install-store.js';
export const MAX_TRACK_ARCHIVE_BYTES=65*1024*1024;
const fail=()=>{throw Object.assign(Error('Invalid Trail Mate package archive'),{code:'TRACK_ARCHIVE_INVALID'});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};
const resourcePath=hash=>'resources/'+packageInstallHex(hash)+'.bin';
function inventory(checked){
 const rows=checked.index?.[3]??[],provided=new Set(checked.payload.attachmentFiles.map(file=>{const row=rows.find(row=>row[0]===file.id);return resourcePath(row[1])+':'+row[2];})),has=row=>provided.has(resourcePath(row[1])+':'+row[2]);
 return {format:'trailmate.track.package',version:1,objectId:packageInstallHex(checked.verified.objectId),revisionHash:packageInstallHex(checked.verified.revisionHash),author:packageInstallHex(checked.verified.record[1]),complete:rows.every(has),
  resources:rows.map(row=>[row[0],packageInstallHex(row[1]),row[2],row[3],row[4],row[5],row[6],has(row),has(row)?resourcePath(row[1]):null])};
}
export async function exportTrackPackageArchive(input,{signal}={}){
 check(signal);const checked=await verifyInstallPayload(input);check(signal);
 const info=inventory(checked),files=Object.create(null);
 files['track.kml']=checked.payload.kml;files['signed-manifest.cmp']=encode(checked.payload.signed);
 if(checked.payload.attachmentIndex)files['attachment-index.cmp']=checked.payload.attachmentIndex;
 for(const file of checked.payload.attachmentFiles){const row=checked.index[3].find(row=>row[0]===file.id);files[resourcePath(row[1])]=file.content;}
 files['package.json']=new TextEncoder().encode(JSON.stringify(info,null,2)+'\n');
 if(files['package.json'].length>524288)fail();
 check(signal);const archive=zipSync(files,{level:0,mtime:new Date(1980,0,1)});check(signal);if(archive.length>MAX_TRACK_ARCHIVE_BYTES)fail();
 return {bytes:archive,filename:'tmtrack_'+info.objectId+'_'+info.revisionHash+'.zip',complete:info.complete,info};
}
// This signed package format uses stored ZIP entries. Admit names/counts/sizes
// before extraction; generic compressed ZIP/KMZ is a separate import format.
export async function readTrackPackageArchive(input,{signal}={}){
 try{
 check(signal);if(!(input instanceof Uint8Array)||!input.length||input.length>MAX_TRACK_ARCHIVE_BYTES)fail();
 const seen=new Set();let total=0;
 const files=unzipSync(input,{filter:entry=>{
  const limit=entry.name==='track.kml'?8388608:entry.name==='signed-manifest.cmp'?4166:entry.name==='attachment-index.cmp'?294912:entry.name==='package.json'?524288:/^resources\/[a-f0-9]{64}\.bin$/.test(entry.name)?8388608:0;
  if(!limit||seen.has(entry.name)||seen.size>=68||entry.compression!==0||!Number.isSafeInteger(entry.originalSize)||entry.originalSize<1||entry.originalSize>limit||entry.originalSize!==entry.size||(total+=entry.originalSize)>MAX_TRACK_ARCHIVE_BYTES)fail();
  seen.add(entry.name);return true;
 }});check(signal);
 if(!files['track.kml']||!files['signed-manifest.cmp']||!files['package.json'])fail();
 let info;try{info=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(files['package.json']));}catch{fail();}
 const signed=decode(files['signed-manifest.cmp'],4166),attachmentIndex=files['attachment-index.cmp'];
 // First verify the signed index and KML. The index supplies all resource paths.
 const {verifyTrackManifest}=await import('./manifest.js'),{matchTrackAttachmentIndex}=await import('./attachment-index.js');
 const proof=await verifyTrackManifest(signed);check(signal);
 let rows=[];if(proof.record[0]===2&&proof.record[15]){if(!attachmentIndex)fail();rows=(await matchTrackAttachmentIndex(attachmentIndex,signed[0])).record[3];}
 const attachmentFiles=[],used=new Set(['track.kml','signed-manifest.cmp','package.json']);if(attachmentIndex)used.add('attachment-index.cmp');
 for(const row of rows){const path=resourcePath(row[1]),content=files[path];if(content&&content.length===row[2]){attachmentFiles.push({id:row[0],content});used.add(path);}}
 if(Object.keys(files).some(name=>!used.has(name)))fail();
 const checked=await verifyInstallPayload({signed,kml:files['track.kml'],attachmentIndex,attachmentFiles});check(signal);
 const expected=inventory(checked);
 if(info?.format!==expected.format||info.version!==1||info.objectId!==expected.objectId||info.revisionHash!==expected.revisionHash||info.author!==expected.author||info.complete!==expected.complete||!equal(encode(info.resources),encode(expected.resources)))fail();
 return {payload:checked.payload,verified:checked.verified,complete:expected.complete,info:expected};
 }catch(error){if(error.name==='AbortError')throw error;fail();}
}
