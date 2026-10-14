import {KML_LIMIT} from './kml.js';
import {decodeTrackJobBinding} from './job-binding.js';
import {assertTrackIPDraftEditable} from './ip-publication-journal.js';
import {withTrackDraftLock} from './draft-lock.js';
import {freezeDraftAttachmentRows,validateDraftAttachmentRows,buildDraftAttachmentIndex,attachmentDigest,attachmentHex} from './draft-attachments.js';
import {verifyInstallPayload} from './package-install-store.js';
import {encode,decode,equal} from '../../geocaching/src/protocol.js';
import {verifyTrackManifest} from './manifest.js';
import {matchTrackAttachmentIndex} from './attachment-index.js';
import {decodeTrackPublicationPlan as decodeRetainedPlan} from './publication-plan.js';
import {validateTrackEditorMetadata} from './track-editor-metadata.js';
import {validateDraftBackupSnapshot} from './draft-backup.js';
import {ORIGINAL_FORMATS,originalFilenameMatches} from './original-format.js';
const NAME='trailmate-track-drafts',CAPACITY=64,BUDGET=64*1024*1024,utf8=new TextEncoder();
function open(){return new Promise((resolve,reject)=>{
 // v4 excludes older writers that would discard the original recording format.
 const request=indexedDB.open(NAME,4);let blocked=false;
 request.onupgradeneeded=()=>{
  const db=request.result;
  if(!db.objectStoreNames.contains('metadata'))db.createObjectStore('metadata',{keyPath:'id'});
  if(!db.objectStoreNames.contains('files'))db.createObjectStore('files',{keyPath:'id'});
  if(!db.objectStoreNames.contains('attachments'))db.createObjectStore('attachments',{keyPath:'id'});
  if(!db.objectStoreNames.contains('jobs')){
   const jobs=db.createObjectStore('jobs',{keyPath:'key'}),cursor=request.transaction.objectStore('metadata').openCursor();
   cursor.onsuccess=()=>{const entry=cursor.result;if(!entry)return;
    try{const row=entry.value;if(row.jobBinding){
     const binding=decodeTrackJobBinding(new Uint8Array(row.jobBinding)),hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
     jobs.put({key:hex(binding.authorHash)+':'+binding.generation+':'+hex(binding.operationId),binding:row.jobBinding,
      draftId:row.id,draftVersion:row.version,name:row.name,createdAt:row.updatedAt??Date.now()});
    }}catch{/* Preserve malformed source metadata for inspection. */}
    entry.continue();};
  }
 };
 request.onblocked=()=>{blocked=true;reject(Error('轨迹存储被另一个页面占用，请关闭旧页面后重试'));};
 request.onerror=()=>reject(request.error);
 request.onsuccess=()=>{const db=request.result;if(blocked){db.close();return;}db.onversionchange=()=>db.close();resolve(db);};
 });}
const get=request=>new Promise((resolve,reject)=>{request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);});
function label(value){if(typeof value!=='string'||!value.trim()||value.includes('\0')||utf8.encode(value).length>128)throw Object.assign(Error('轨迹名称需要 1–128 个 UTF-8 字节'),{code:'DRAFT_NAME_INVALID'});return value;}
export async function listTrackDrafts(){const db=await open();try{return (await get(db.transaction('metadata').objectStore('metadata').getAll())).sort((a,b)=>b.updatedAt-a.updatedAt);}finally{db.close();}}
async function readOriginalSource(metadata,file){
 if(metadata.originalBytes===undefined){if(file?.original!==undefined||metadata.originalFormat!==undefined)throw Object.assign(Error('Unexpected original'),{code:'DRAFT_CORRUPT'});return undefined;}
 const format=metadata.originalFormat===undefined?'kml':metadata.originalFormat;
 if(typeof format!=='string'||!Object.hasOwn(ORIGINAL_FORMATS,format)||!originalFilenameMatches(metadata.archiveMember,format))throw Object.assign(Error('Original format is corrupt'),{code:'DRAFT_CORRUPT'});
 if(!(file?.original instanceof ArrayBuffer)||file.original.byteLength!==metadata.originalBytes||!metadata.originalBytes||metadata.originalBytes>KML_LIMIT.bytes||typeof metadata.archiveMember!=='string'||!metadata.archiveMember||metadata.archiveMember.includes('\0')||utf8.encode(metadata.archiveMember).length>4096)throw Object.assign(Error('Original is corrupt'),{code:'DRAFT_CORRUPT'});
 const source=new Uint8Array(file.original);if(attachmentHex(await attachmentDigest(source))!==metadata.originalHash)throw Object.assign(Error('Original digest differs'),{code:'DRAFT_CORRUPT'});return source;
}
export async function readTrackDraft(id){const db=await open();try{
 const tx=db.transaction(['metadata','files','attachments']),[metadata,file,assets]=await Promise.all([
  get(tx.objectStore('metadata').get(id)),get(tx.objectStore('files').get(id)),get(tx.objectStore('attachments').get(id))]);
 if(!metadata||!file)throw Object.assign(Error('浏览器草稿不存在或文件缺失'),{code:'DRAFT_CORRUPT'});
 if(!(file.bytes instanceof ArrayBuffer)||file.bytes.byteLength!==metadata.bytes||file.bytes.byteLength>KML_LIMIT.bytes)throw Object.assign(Error('草稿文件大小不匹配'),{code:'DRAFT_CORRUPT'});
 const source=new Uint8Array(file.bytes),hash=attachmentHex(await attachmentDigest(source));
 if(hash!==metadata.hash)throw Object.assign(Error('草稿文件哈希不匹配，请保留文件并检查存储'),{code:'DRAFT_CORRUPT'});
 const originalSource=await readOriginalSource(metadata,file);
 let attachmentRows;
 try{
  if(metadata.editorMetadata!==undefined)validateTrackEditorMetadata(metadata.editorMetadata);
  attachmentRows=(assets?.rows??[]).map(row=>({...row,content:row.content instanceof ArrayBuffer?new Uint8Array(row.content):null}));
  const total=validateDraftAttachmentRows(attachmentRows);
  if(metadata.attachmentBindings!==undefined&&(!(metadata.attachmentBindings instanceof ArrayBuffer)||!metadata.attachmentBindings.byteLength||metadata.attachmentBindings.byteLength>6144||!metadata.publicationPlan))throw Error('Invalid attachment bindings');
  if(attachmentRows.length!==(metadata.attachmentCount??0)||total!==(metadata.attachmentBytes??0)||metadata.bytes+total+(metadata.originalBytes??0)>BUDGET)throw Error('Attachment totals differ');
  for(const row of attachmentRows)if(row.hash!==attachmentHex(await attachmentDigest(row.content)))throw Error('Attachment digest differs');
  if(metadata.sourceProof!==undefined){
   if(!(metadata.sourceProof instanceof ArrayBuffer)||metadata.sourceProof.byteLength>4352)throw Error('Invalid source proof');
   const proof=await verifyTrackManifest(decode(new Uint8Array(metadata.sourceProof),4352));
   if(metadata.attachmentTemplate!==undefined){if(!(metadata.attachmentTemplate instanceof ArrayBuffer))throw Error('Invalid source index');const index=new Uint8Array(metadata.attachmentTemplate);await matchTrackAttachmentIndex(index,proof.manifest);await buildDraftAttachmentIndex(source,attachmentRows,index);}
  }else if(metadata.attachmentTemplate!==undefined)throw Error('Missing source proof');
 }catch{throw Object.assign(Error('草稿附件损坏或缺失'),{code:'DRAFT_CORRUPT'});}
 return {...metadata,source,attachmentRows,originalSource};
}finally{db.close();}}
export async function saveTrackDraft(source,{id=crypto.randomUUID(),expectedVersion=0,name,filename='track.kml',publicationPlan,predecessor,archive=false,packageSource,archiveImport,originalImport,draftRestore}={}){
 // Capture imported bytes before waiting for the draft lock or hashes.
 // Later saves cannot replace this original; legacy restores are not invented.
 if(originalImport!==undefined){
  const input=originalImport?.source,member=originalImport?.filename,format=originalImport?.format===undefined?'kml':originalImport.format;
  if(!originalImport||typeof originalImport!=='object'||Array.isArray(originalImport)||Object.keys(originalImport).some(key=>!['source','filename','format'].includes(key))||typeof format!=='string'||!Object.hasOwn(ORIGINAL_FORMATS,format)||expectedVersion!==0||archiveImport!==undefined||draftRestore!==undefined||predecessor!==undefined||archive||!(source instanceof Uint8Array)||!(input instanceof Uint8Array)||!input.length||input.length>KML_LIMIT.bytes||typeof member!=='string'||!member.trim()||member.includes('\0')||utf8.encode(member).length>4096||!originalFilenameMatches(member,format)||(format==='kml'?!equal(input,source):packageSource!==undefined))throw Object.assign(Error('Invalid imported original'),{code:'DRAFT_ORIGINAL_INVALID'});
  originalImport=Object.freeze({source:input.slice(),filename:member,format});source=source.slice();
 }
 return withTrackDraftLock(id,async()=>{
 if(!(source instanceof Uint8Array)||!source.length||source.length>KML_LIMIT.bytes||typeof id!=='string'||!id||!Number.isSafeInteger(expectedVersion)||expectedVersion<0)throw Error('轨迹草稿保存参数无效');
 name=label(name);filename=label(filename);const bytes=source.slice();
 let retainedOriginal;
 if(expectedVersion>0){const previousDb=await open();try{const tx=previousDb.transaction(['metadata','files']),[previous,file]=await Promise.all([get(tx.objectStore('metadata').get(id)),get(tx.objectStore('files').get(id))]);if(previous?.version!==expectedVersion)throw Object.assign(Error('Draft changed'),{code:'DRAFT_CONFLICT'});const original=await readOriginalSource(previous,file);if(original)retainedOriginal={source:original,hash:previous.originalHash,member:previous.archiveMember,format:previous.originalFormat??'kml'};}finally{previousDb.close();}}
 const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');
 let imported,restored,attachmentRows,attachmentBytes=0;
 let original,originalHash;
 if(originalImport){original=originalImport.source;originalHash=attachmentHex(await attachmentDigest(original));}
 if(draftRestore!==undefined){
  if(expectedVersion!==0||packageSource!==undefined||archiveImport!==undefined||predecessor!==undefined||publicationPlan!==undefined||archive)throw Error('Backup restore is only for a new draft');
  restored=await validateDraftBackupSnapshot(draftRestore);
  if(!equal(restored.source,bytes)||restored.name!==name||restored.filename!==filename)throw Error('Backup restore differs from draft');
  attachmentRows=restored.attachmentRows;attachmentBytes=validateDraftAttachmentRows(attachmentRows);
  original=restored.originalSource;if(original)originalHash=attachmentHex(await attachmentDigest(original));
 }
 if(archiveImport!==undefined){
  if(expectedVersion!==0||packageSource!==undefined||predecessor!==undefined||publicationPlan!==undefined||archive||!(archiveImport.originalKml instanceof Uint8Array)||!archiveImport.originalKml.length||archiveImport.originalKml.length>KML_LIMIT.bytes||typeof archiveImport.member!=='string'||!archiveImport.member||archiveImport.member.includes('\0')||utf8.encode(archiveImport.member).length>4096)throw Error('Invalid unsigned archive import');
  original=archiveImport.originalKml.slice();originalHash=attachmentHex(await attachmentDigest(original));
  attachmentRows=await freezeDraftAttachmentRows(archiveImport.attachments);attachmentBytes=validateDraftAttachmentRows(attachmentRows);
 }
 if(packageSource!==undefined){
  if(expectedVersion!==0)throw Error('Source package is only accepted for a new draft');
  imported=await verifyInstallPayload(packageSource);
  attachmentRows=await freezeDraftAttachmentRows(imported.payload.attachmentFiles.map(file=>{const entry=imported.index[3].find(row=>row[0]===file.id);return {id:file.id,content:file.content,mime:entry[3],reference:entry[4],role:entry[5],required:entry[6]===1};}));
  attachmentBytes=validateDraftAttachmentRows(attachmentRows);
 }
 let planBytes;
 let predecessorBytes=restored?.predecessor;
 if(predecessor!==undefined){
  const {verifyTrackManifest}=await import('./manifest.js'),{encode}=await import('../../geocaching/src/protocol.js');
  const previous=await verifyTrackManifest(predecessor);
  if(imported&&!equal(previous.revisionHash,imported.verified.revisionHash))throw Error('Source package differs from predecessor');
  if(previous.record[6]!==0||typeof archive!=='boolean')throw Error('后续草稿需要活动的签名父版本');
  if(archive&&(previous.record[13][1]!==bytes.length||Array.from(previous.record[13][0],b=>b.toString(16).padStart(2,'0')).join('')!==hash))throw Error('归档草稿必须保留原文件');
  predecessorBytes=encode([previous.manifest,previous.signature]);
 }
 if(publicationPlan!==undefined){
  const {encodeTrackPublicationPlan,decodeTrackPublicationPlan}=await import('./publication-plan.js');
  planBytes=encodeTrackPublicationPlan(publicationPlan);const plan=decodeTrackPublicationPlan(planBytes);
  if(plan.fileBytes!==bytes.length||Array.from(plan.fileHash,b=>b.toString(16).padStart(2,'0')).join('')!==hash)throw Error('轨迹草稿与发布计划文件绑定不一致');
 }
 const db=await open();
 try{return await new Promise((resolve,reject)=>{
  const tx=db.transaction(['metadata','files','attachments'],'readwrite'),store=tx.objectStore('metadata'),request=store.getAll();let metadata,failure;
  tx.oncomplete=()=>resolve(metadata);tx.onabort=()=>reject(failure??tx.error??Error('轨迹草稿保存未完成'));tx.onerror=()=>{};
  request.onsuccess=()=>{
   try{
    const rows=request.result,existing=rows.find(row=>row.id===id);
    if(restored&&existing)throw Object.assign(Error('Restore requires a new draft ID'),{code:'DRAFT_CONFLICT'});
    if((existing?.version??0)!==expectedVersion)throw Object.assign(Error('轨迹草稿已被其他页面修改，请重新打开后合并'),{code:'DRAFT_CONFLICT'});
    if(existing?.originalBytes!==undefined?(!retainedOriginal||retainedOriginal.source.length!==existing.originalBytes||retainedOriginal.hash!==existing.originalHash||retainedOriginal.member!==existing.archiveMember||retainedOriginal.format!==(existing.originalFormat??'kml')):Boolean(retainedOriginal))throw Object.assign(Error('Original binding changed'),{code:'DRAFT_CORRUPT'});
    if(existing)assertTrackIPDraftEditable(id,existing.version);
    if(existing?.predecessor&&predecessorBytes)throw Error('编辑不能替换草稿的签名父版本');
    if(existing?.archive&&existing.hash!==hash)throw Error('归档草稿不能更换 KML 文件');
    if(!existing&&rows.length>=CAPACITY||rows.filter(row=>row.id!==id).reduce((size,row)=>size+row.bytes+(row.attachmentBytes??0)+(row.originalBytes??0),0)+bytes.length+((imported||archiveImport||restored)?attachmentBytes:existing?.attachmentBytes??0)+(original?.length??existing?.originalBytes??0)>BUDGET)throw Object.assign(Error('浏览器轨迹草稿空间已满，请先导出或删除旧草稿'),{code:'DRAFT_FULL'});
    metadata={id,name,filename,bytes:bytes.length,hash,version:expectedVersion+1,updatedAt:Date.now()};
    if(existing?.attachmentCount!==undefined){metadata.attachmentCount=existing.attachmentCount;metadata.attachmentBytes=existing.attachmentBytes;}
    if(existing?.sourceProof)metadata.sourceProof=existing.sourceProof;
    if(existing?.editorMetadata!==undefined)metadata.editorMetadata=validateTrackEditorMetadata(existing.editorMetadata);
    if(existing?.attachmentTemplate)metadata.attachmentTemplate=existing.attachmentTemplate;
    if(imported){metadata.sourceProof=encode(imported.payload.signed).buffer;metadata.attachmentCount=attachmentRows.length;metadata.attachmentBytes=attachmentBytes;if(imported.payload.attachmentIndex)metadata.attachmentTemplate=imported.payload.attachmentIndex.buffer;tx.objectStore('attachments').put({id,rows:attachmentRows.map(row=>({...row,content:row.content.buffer}))});}
    if(restored){metadata.editorMetadata=restored.editorMetadata;if(restored.sourceProof)metadata.sourceProof=restored.sourceProof.buffer;if(restored.attachmentTemplate)metadata.attachmentTemplate=restored.attachmentTemplate.buffer;metadata.attachmentCount=attachmentRows.length;metadata.attachmentBytes=attachmentBytes;tx.objectStore('attachments').put({id,rows:attachmentRows.map(row=>({...row,content:row.content.buffer}))});}
    if(original){metadata.originalBytes=original.length;metadata.originalHash=originalHash;metadata.archiveMember=originalImport?.filename??archiveImport?.member??restored.archiveMember;if(archiveImport||restored?.originalSource){metadata.attachmentCount=attachmentRows.length;metadata.attachmentBytes=attachmentBytes;tx.objectStore('attachments').put({id,rows:attachmentRows.map(row=>({...row,content:row.content.buffer}))});}}
    else if(existing?.originalBytes!==undefined){metadata.originalBytes=existing.originalBytes;metadata.originalHash=existing.originalHash;metadata.archiveMember=existing.archiveMember;}
    const sourceFormat=originalImport?.format??restored?.originalFormat??existing?.originalFormat;if(sourceFormat&&sourceFormat!=='kml')metadata.originalFormat=sourceFormat;
    if(planBytes)metadata.publicationPlan=planBytes.buffer;
    if(predecessorBytes){metadata.predecessor=predecessorBytes.buffer;metadata.archive=restored?.archive??archive;}
    else if(existing?.predecessor){metadata.predecessor=existing.predecessor;metadata.archive=existing.archive===true;}
    store.put(metadata);
    if(existing?.originalBytes!==undefined)tx.objectStore('files').put({id,bytes:bytes.buffer,original:retainedOriginal.source.buffer});
    else tx.objectStore('files').put({id,bytes:bytes.buffer,...(original?{original:original.buffer}:{})});
   }catch(error){failure=error;tx.abort();}
  };
 });}finally{db.close();}
 });
}
export async function removeTrackDraft(id,expectedVersion){
 return withTrackDraftLock(id,async()=>{
 const db=await open();try{return await new Promise((resolve,reject)=>{
  const tx=db.transaction(['metadata','files','attachments'],'readwrite'),store=tx.objectStore('metadata'),request=store.get(id);let failure;
  tx.oncomplete=()=>resolve();tx.onabort=()=>reject(failure??tx.error??Error('草稿删除未完成'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{if(!request.result||request.result.version!==expectedVersion)throw Object.assign(Error('轨迹草稿版本已变化，请刷新列表'),{code:'DRAFT_CONFLICT'});assertTrackIPDraftEditable(id,expectedVersion);store.delete(id);tx.objectStore('files').delete(id);tx.objectStore('attachments').delete(id);}catch(error){failure=error;tx.abort();}};
 });}finally{db.close();}
 });
}
export async function saveTrackDraftAttachments(id,expectedVersion,rows){
 return withTrackDraftLock(id,async()=>{
  if(typeof id!=='string'||!id||!Number.isSafeInteger(expectedVersion)||expectedVersion<1)throw Error('Invalid attachment draft selection');
  const frozen=await freezeDraftAttachmentRows(rows),attachmentBytes=validateDraftAttachmentRows(frozen),db=await open();
  try{return await new Promise((resolve,reject)=>{
   const tx=db.transaction(['metadata','attachments'],'readwrite'),store=tx.objectStore('metadata'),request=store.getAll();let metadata,failure;
   tx.oncomplete=()=>resolve(metadata);tx.onabort=()=>reject(failure??tx.error??Error('Attachment save failed'));tx.onerror=()=>{};
   request.onsuccess=()=>{try{
    const all=request.result,row=all.find(value=>value.id===id);
    if(!row||row.version!==expectedVersion)throw Object.assign(Error('Draft version changed'),{code:'DRAFT_CONFLICT'});
    assertTrackIPDraftEditable(id,expectedVersion);
    if(row.archive)throw Object.assign(Error('Archived successor retains original resources'),{code:'ATTACHMENT_ARCHIVE'});
    if(all.reduce((sum,value)=>sum+value.bytes+(value.originalBytes??0)+(value.id===id?attachmentBytes:value.attachmentBytes??0),0)>BUDGET)throw Object.assign(Error('Draft storage full'),{code:'DRAFT_FULL'});
    metadata={id:row.id,name:row.name,filename:row.filename,hash:row.hash,bytes:row.bytes,version:row.version+1,updatedAt:Date.now(),attachmentCount:frozen.length,attachmentBytes};
    if(row.originalBytes!==undefined){metadata.originalBytes=row.originalBytes;metadata.originalHash=row.originalHash;metadata.archiveMember=row.archiveMember;if(row.originalFormat!==undefined)metadata.originalFormat=row.originalFormat;}
    if(row.predecessor){metadata.predecessor=row.predecessor;metadata.archive=false;}
    if(row.sourceProof)metadata.sourceProof=row.sourceProof;
    if(row.editorMetadata!==undefined)metadata.editorMetadata=validateTrackEditorMetadata(row.editorMetadata);
    store.put(metadata);
    if(frozen.length)tx.objectStore('attachments').put({id,rows:frozen.map(value=>({...value,content:value.content.buffer}))});
    else tx.objectStore('attachments').delete(id);
   }catch(error){failure=error;tx.abort();}};
  });}finally{db.close();}
 });
}
export async function retainTrackAttachmentBindings(id,expectedVersion,manifest,bindings){
 if(!(bindings instanceof Uint8Array)||!bindings.length||bindings.length>6144||!(manifest instanceof Uint8Array)||!manifest.length||manifest.length>4096)throw Error('Invalid attachment binding bytes');
 const {equal}=await import('../../geocaching/src/protocol.js'),db=await open();
 try{return await new Promise((resolve,reject)=>{
  const tx=db.transaction('metadata','readwrite'),store=tx.objectStore('metadata'),request=store.get(id);let retained,failure;
  tx.oncomplete=()=>resolve(retained);tx.onabort=()=>reject(failure??tx.error??Error('Attachment bindings not saved'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{
   const row=request.result;
   if(!row||row.version!==expectedVersion||!row.publicationPlan)throw Object.assign(Error('Draft binding changed'),{code:'DRAFT_CONFLICT'});
   const {manifest:original}=decodeRetainedPlan(new Uint8Array(row.publicationPlan));
   if(!equal(original,manifest))throw Error('Attachment manifest changed');
   retained=row.attachmentBindings?new Uint8Array(row.attachmentBindings):bindings.slice();
   if(!row.attachmentBindings)store.put({...row,attachmentBindings:retained.buffer});
  }catch(error){failure=error;tx.abort();}};
 });}finally{db.close();}
}
export async function saveTrackDraftMetadata(id,expectedVersion,value){
 const editorMetadata=validateTrackEditorMetadata(value);
 return withTrackDraftLock(id,async()=>{
  if(typeof id!=='string'||!id||!Number.isSafeInteger(expectedVersion)||expectedVersion<1)throw Error('Invalid metadata draft selection');
  const db=await open();try{return await new Promise((resolve,reject)=>{
   const tx=db.transaction('metadata','readwrite'),store=tx.objectStore('metadata'),request=store.get(id);let metadata,failure;
   tx.oncomplete=()=>resolve(metadata);tx.onabort=()=>reject(failure??tx.error??Error('Metadata save failed'));tx.onerror=()=>{};
   request.onsuccess=()=>{try{
    const row=request.result;
    if(!row||row.version!==expectedVersion)throw Object.assign(Error('Draft version changed'),{code:'DRAFT_CONFLICT'});
    assertTrackIPDraftEditable(id,expectedVersion);
    if(row.archive)throw Object.assign(Error('Archive retains original metadata'),{code:'DRAFT_METADATA_ARCHIVE'});
    metadata={...row,editorMetadata,version:row.version+1,updatedAt:Date.now()};
    delete metadata.publicationPlan;delete metadata.attachmentBindings;delete metadata.jobBinding;
    store.put(metadata);
   }catch(error){failure=error;tx.abort();}};
  });}finally{db.close();}
 });
}
export async function saveTrackDraftPublicationPlan(id,expectedVersion,plan){
 const {encodeTrackPublicationPlan,decodeTrackPublicationPlan}=await import('./publication-plan.js');
 const raw=encodeTrackPublicationPlan(plan),retained=decodeTrackPublicationPlan(raw);
 const hash=Array.from(retained.fileHash,b=>b.toString(16).padStart(2,'0')).join('');
 if(typeof id!=='string'||!id||!Number.isSafeInteger(expectedVersion)||expectedVersion<1)throw Error('轨迹准备草稿版本无效');
 const db=await open();
 try{return await new Promise((resolve,reject)=>{
  const tx=db.transaction('metadata','readwrite'),store=tx.objectStore('metadata'),request=store.get(id);let metadata,failure;
  tx.oncomplete=()=>resolve(metadata);tx.onabort=()=>reject(failure??tx.error??Error('轨迹准备计划保存未完成'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{
   const row=request.result;
   if(!row||row.version!==expectedVersion||row.hash!==hash||row.bytes!==retained.fileBytes)throw Error('轨迹草稿已变化，请重新打开后准备');
   if(row.publicationPlan){
    const previous=new Uint8Array(row.publicationPlan);
    if(previous.length!==raw.length||previous.some((value,i)=>value!==raw[i]))throw Error('草稿已有不同的准备计划，请先完成或清理原操作');
    metadata=row;return;
   }
   metadata={...row,publicationPlan:raw.buffer};store.put(metadata);
  }catch(error){failure=error;tx.abort();}};
 });}finally{db.close();}
}
export async function saveTrackDraftJobBinding(id,expectedVersion,binding){
 const {encodeTrackJobBinding,decodeTrackJobBinding}=await import('./job-binding.js');
 const {decodeTrackPublicationPlan}=await import('./publication-plan.js');
 const {decode,equal}=await import('../../geocaching/src/protocol.js');
 const raw=encodeTrackJobBinding(binding),retained=decodeTrackJobBinding(raw);
 const snapshot=await readTrackDraft(id);
 if(snapshot.version!==expectedVersion||!snapshot.publicationPlan)throw Error('轨迹准备计划或草稿版本已变化');
 const plan=decodeTrackPublicationPlan(new Uint8Array(snapshot.publicationPlan)),record=decode(plan.manifest,4096);
 const hash=async(...parts)=>{
  const input=new Uint8Array(parts.reduce((size,part)=>size+part.length,0));let offset=0;
  for(const part of parts){input.set(part,offset);offset+=part.length;}
  return new Uint8Array(await crypto.subtle.digest('SHA-256',input));
 };
 const [authorHash,objectId,revisionHash]=await Promise.all([hash(record[1]),hash(utf8.encode('TrailMate.Track.ID.v1\0'),record[1],record[2]),
  hash(utf8.encode('TrailMate.Track.Revision.v1\0'),plan.manifest)]);
 if(!equal(retained.operationId,plan.operationId)||retained.generation!==plan.generation||retained.revision!==record[4]||
  !equal(retained.authorHash,authorHash)||!equal(retained.objectId,objectId)||!equal(retained.revisionHash,revisionHash)||
  !equal(retained.fileHash,plan.fileHash)||retained.fileBytes!==plan.fileBytes)throw Error('轨迹任务与已保存准备计划不一致');
 const db=await open();
 try{return await new Promise((resolve,reject)=>{
  const tx=db.transaction(['metadata','jobs'],'readwrite'),store=tx.objectStore('metadata'),jobs=tx.objectStore('jobs'),request=store.get(id);let metadata,failure;
  tx.oncomplete=()=>resolve(metadata);tx.onabort=()=>reject(failure??tx.error??Error('轨迹任务绑定保存未完成'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{
   const row=request.result;
   if(!row||row.version!==expectedVersion||row.hash!==snapshot.hash||!row.publicationPlan||
    !equal(new Uint8Array(row.publicationPlan),new Uint8Array(snapshot.publicationPlan)))throw Error('轨迹草稿或准备计划已变化');
   if(row.jobBinding&&!equal(new Uint8Array(row.jobBinding),raw))throw Error('已有不同轨迹任务绑定，不能覆盖原操作');
   const hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
   const key=hex(retained.authorHash)+':'+retained.generation+':'+hex(retained.operationId);
   const existing=jobs.get(key);
   existing.onsuccess=()=>{try{
    if(existing.result&&!equal(new Uint8Array(existing.result.binding),raw))throw Error('已有不同任务历史，不能覆盖');
    const persist=()=>{metadata={...row,jobBinding:raw.buffer};store.put(metadata);
     if(!existing.result)jobs.put({key,binding:raw.buffer,draftId:id,draftVersion:expectedVersion,name:row.name,createdAt:Date.now()});};
    if(existing.result){persist();return;}
    const count=jobs.count();count.onsuccess=()=>{try{if(count.result>=256)throw Error('任务历史已满，请先导出并清理历史');persist();}catch(error){failure=error;tx.abort();}};
   }catch(error){failure=error;tx.abort();}};
  }catch(error){failure=error;tx.abort();}};
 });}finally{db.close();}
}
export async function listTrackJobHistory(){
 const db=await open();try{return (await get(db.transaction('jobs').objectStore('jobs').getAll())).sort((a,b)=>b.createdAt-a.createdAt);}finally{db.close();}
}
export async function readTrackJobHistory(key){
 if(typeof key!=='string'||!key||key.length>128)throw Error('任务历史标识无效');
 const db=await open();try{
  const row=await get(db.transaction('jobs').objectStore('jobs').get(key));
  if(!row)throw Error('任务历史不存在');
  const binding=decodeTrackJobBinding(new Uint8Array(row.binding)),hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
  if(key!==hex(binding.authorHash)+':'+binding.generation+':'+hex(binding.operationId))throw Error('任务历史标识与绑定不一致');
  return {...row,binding};
 }finally{db.close();}
}
function historyError(code,message,cause){const error=Error(message,{cause});error.code=code;return error;}
export async function removeTrackJobHistory(key,expectedBinding){
 const {equal}=await import('../../geocaching/src/protocol.js');
 if(typeof key!=='string'||!key||key.length>128||!(expectedBinding instanceof Uint8Array))throw Error('任务历史删除参数无效');
 const expected=expectedBinding.slice();decodeTrackJobBinding(expected);
 const db=await open();try{return await new Promise((resolve,reject)=>{
  const tx=db.transaction('jobs','readwrite'),store=tx.objectStore('jobs'),request=store.get(key);let failure;
  tx.oncomplete=()=>resolve();tx.onabort=()=>reject(failure??tx.error??Error('历史删除未完成'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{
   if(!request.result||!equal(new Uint8Array(request.result.binding),expected))throw historyError('HISTORY_CHANGED','任务历史已变化，请刷新后重试');
   store.delete(key);
  }catch(error){failure=error;tx.abort();}};
 });}finally{db.close();}
}
export async function importTrackJobHistory(source,{name='导入任务'}={}){
 const {equal}=await import('../../geocaching/src/protocol.js');
 let binding;try{binding=decodeTrackJobBinding(source);}catch(error){throw historyError('HISTORY_INVALID','任务绑定文件无效',error);}
 const raw=source.slice(),hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
 const key=hex(binding.authorHash)+':'+binding.generation+':'+hex(binding.operationId),title=label(name);
 const db=await open();try{return await new Promise((resolve,reject)=>{
  const tx=db.transaction('jobs','readwrite'),store=tx.objectStore('jobs'),request=store.get(key);let row,failure;
  tx.oncomplete=()=>resolve(row);tx.onabort=()=>reject(failure??tx.error??Error('任务导入未完成'));tx.onerror=()=>{};
  request.onsuccess=()=>{try{
   if(request.result){if(!equal(new Uint8Array(request.result.binding),raw))throw historyError('HISTORY_CONFLICT','已有不同任务绑定，不能覆盖原操作');row=request.result;return;}
   const count=store.count();count.onsuccess=()=>{try{
    if(count.result>=256)throw historyError('HISTORY_FULL','任务历史已满，请先导出并清理历史');
    row={key,binding:raw.buffer,name:title,createdAt:Date.now(),draftId:null,draftVersion:null};store.put(row);
   }catch(error){failure=error;tx.abort();}};
  }catch(error){failure=error;tx.abort();}};
 });}finally{db.close();}
}
