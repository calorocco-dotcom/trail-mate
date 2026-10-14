import {zipSync} from 'fflate';
import {readKmlArchive} from './kml-archive.js';
import {attachmentDigest,attachmentHex,freezeDraftAttachmentRows,validateDraftAttachmentRows,buildDraftAttachmentIndex} from './draft-attachments.js';
import {draftTrackEditorMetadata,validateTrackEditorMetadata} from './track-editor-metadata.js';
import {verifyTrackManifest} from './manifest.js';
import {matchTrackAttachmentIndex} from './attachment-index.js';
import {encode,decode,equal} from '../../geocaching/src/protocol.js';
import {originalFormat as checkOriginalFormat,originalFilenameMatches} from './original-format.js';
export const DRAFT_BACKUP_LIMIT=65*1024*1024;
const fail=()=>{throw Object.assign(Error('Invalid draft backup'),{code:'DRAFT_BACKUP_INVALID'});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
const utf8=new TextEncoder(),text=new TextDecoder('utf-8',{fatal:true});
const hex=value=>attachmentHex(value);
function label(value,limit=128){if(typeof value!=='string'||!value.trim()||value.includes('\0')||utf8.encode(value).length>limit)fail();return value;}
function binary(value,limit){const bytes=value instanceof ArrayBuffer?new Uint8Array(value):value;if(!(bytes instanceof Uint8Array)||!bytes.length||bytes.length>limit)fail();return bytes.slice();}

// Only durable content and public provenance. Jobs, sessions, prepared nonces,
// upload bindings and author credentials are deliberately outside this format.
export async function validateDraftBackupSnapshot(input){
 if(!input||typeof input!=='object')fail();
 const source=binary(input.source,8388608),name=label(input.name),filename=label(input.filename),archive=input.archive??false;
 if(typeof archive!=='boolean')fail();
 const originalSource=input.originalSource===undefined?undefined:binary(input.originalSource,8388608);
 const archiveMember=originalSource?label(input.archiveMember,4096):undefined;
 if(!originalSource&&input.archiveMember!==undefined)fail();
 const originalFormat=input.originalFormat===undefined?'kml':input.originalFormat;checkOriginalFormat(originalFormat);
 if(!originalSource&&input.originalFormat!==undefined||originalSource&&!originalFilenameMatches(archiveMember,originalFormat))fail();
 const sourceProof=input.sourceProof===undefined?undefined:binary(input.sourceProof,4352),attachmentTemplate=input.attachmentTemplate===undefined?undefined:binary(input.attachmentTemplate,294912),predecessor=input.predecessor===undefined?undefined:binary(input.predecessor,4352);
 const editorMetadata=draftTrackEditorMetadata({editorMetadata:input.editorMetadata,sourceProof,predecessor});
 const rows=await freezeDraftAttachmentRows(input.attachmentRows??[]);
 if(source.length+(originalSource?.length??0)+validateDraftAttachmentRows(rows)>64*1024*1024)fail();
 if(sourceProof){const proof=await verifyTrackManifest(decode(sourceProof,4352));if(attachmentTemplate){await matchTrackAttachmentIndex(attachmentTemplate,proof.manifest);await buildDraftAttachmentIndex(source,rows,attachmentTemplate);}}
 else if(attachmentTemplate)fail();
 if(predecessor){const proof=await verifyTrackManifest(decode(predecessor,4352));if(proof.record[6]!==0)fail();if(archive&&(source.length!==proof.record[13][1]||!equal(await attachmentDigest(source),proof.record[13][0])))fail();}
 else if(archive)fail();
 return {source,name,filename,attachmentRows:rows,editorMetadata:validateTrackEditorMetadata(editorMetadata),originalSource,archiveMember,...(originalFormat!=='kml'?{originalFormat}:{}),sourceProof,attachmentTemplate,predecessor,archive};
}
export async function exportDraftBackup(input,{signal}={}){
 check(signal);const snapshot=await validateDraftBackupSnapshot(input);check(signal);
 const files=Object.create(null),descriptors={};
 async function add(path,bytes){files[path]=bytes;return {path,size:bytes.length,hash:hex(await attachmentDigest(bytes))};}
 descriptors.source=await add('draft.kml',snapshot.source);
 for(const [key,path] of [['originalSource','original.'+(snapshot.originalFormat??'kml')],['sourceProof','source-proof.cmp'],['attachmentTemplate','source-index.cmp'],['predecessor','parent-proof.cmp']])descriptors[key]=snapshot[key]?await add(path,snapshot[key]):null;
 const attachments=[];
 for(const row of snapshot.attachmentRows){check(signal);const hash=row.hash,path='resources/'+hash+'.bin';if(!files[path])files[path]=row.content;attachments.push([row.id,row.reference,row.mime,row.role,row.required,hash,row.content.length,path]);}
 const info={format:'trailmate.track.draft',version:snapshot.originalFormat?2:1,...(snapshot.originalFormat?{originalFormat:snapshot.originalFormat}:{}),name:snapshot.name,filename:snapshot.filename,editorMetadata:snapshot.editorMetadata,archive:snapshot.archive,archiveMember:snapshot.archiveMember??null,files:descriptors,attachments};
 files['draft.json']=utf8.encode(JSON.stringify(info)+'\n');if(files['draft.json'].length>524288)fail();
 check(signal);const bytes=zipSync(files,{level:0,mtime:new Date(1980,0,1)});if(bytes.length>DRAFT_BACKUP_LIMIT)fail();check(signal);
 return {bytes,filename:'trailmate-draft-'+descriptors.source.hash.slice(0,16)+'.zip',info};
}
export async function readDraftBackup(input,{signal}={}){
 try{
  check(signal);const archive=await readKmlArchive(input,{signal,profile:'draft'}),raw=archive.files.get('draft.json');if(!raw||raw.length>524288)fail();
  const info=JSON.parse(text.decode(raw));
  const keys=info?.version===2?'archive,archiveMember,attachments,editorMetadata,filename,files,format,name,originalFormat,version':'archive,archiveMember,attachments,editorMetadata,filename,files,format,name,version';
  if(!info||Object.keys(info).sort().join(',')!==keys||info.format!=='trailmate.track.draft'||![1,2].includes(info.version)||!info.files||Object.keys(info.files).sort().join(',')!=='attachmentTemplate,originalSource,predecessor,source,sourceProof'||!Array.isArray(info.attachments)||info.attachments.length>64)fail();
  const originalFormat=info.version===2?info.originalFormat:'kml';checkOriginalFormat(originalFormat);if(info.version===2&&(originalFormat==='kml'||!info.files.originalSource))fail();
  const used=new Set(['draft.json']);
  async function get(descriptor,path,limit){
   if(descriptor===null)return undefined;
   if(!descriptor||Object.keys(descriptor).sort().join(',')!=='hash,path,size'||descriptor.path!==path||!Number.isSafeInteger(descriptor.size)||descriptor.size<1||descriptor.size>limit||typeof descriptor.hash!=='string'||!/^[a-f0-9]{64}$/.test(descriptor.hash))fail();
   const bytes=archive.files.get(path);if(!bytes||bytes.length!==descriptor.size||hex(await attachmentDigest(bytes))!==descriptor.hash)fail();used.add(path);return bytes;
  }
  const source=await get(info.files.source,'draft.kml',8388608);if(!source)fail();
  const originalSource=await get(info.files.originalSource,'original.'+originalFormat,8388608),sourceProof=await get(info.files.sourceProof,'source-proof.cmp',4352),attachmentTemplate=await get(info.files.attachmentTemplate,'source-index.cmp',294912),predecessor=await get(info.files.predecessor,'parent-proof.cmp',4352);
  const attachmentRows=[];
  for(const row of info.attachments){
   check(signal);if(!Array.isArray(row)||row.length!==8||typeof row[5]!=='string'||!/^[a-f0-9]{64}$/.test(row[5])||row[7]!=='resources/'+row[5]+'.bin'||!Number.isSafeInteger(row[6])||row[6]<1||row[6]>8388608)fail();
   const content=archive.files.get(row[7]);if(!content||content.length!==row[6]||hex(await attachmentDigest(content))!==row[5])fail();used.add(row[7]);attachmentRows.push({id:row[0],reference:row[1],mime:row[2],role:row[3],required:row[4],content});
  }
  if([...archive.files.keys()].some(name=>!used.has(name))||info.archiveMember!==null&&!originalSource)fail();
  const snapshot=await validateDraftBackupSnapshot({source,name:info.name,filename:info.filename,editorMetadata:info.editorMetadata,archive:info.archive,archiveMember:info.archiveMember??undefined,originalSource,...(info.version===2?{originalFormat}:{}),sourceProof,attachmentTemplate,predecessor,attachmentRows});check(signal);return snapshot;
 }catch(error){if(signal?.aborted)check(signal);fail();}
}
