import {encodeTrackAttachmentIndex,decodeTrackAttachmentIndex,attachmentReferenceValid} from './attachment-index.js';
import {equal} from '../../geocaching/src/protocol.js';
import {MAX_TRACK_ATTACHMENTS,MAX_TRACK_PACKAGE_BYTES} from './manifest-extensions.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
export const attachmentDigest=async content=>new Uint8Array(await crypto.subtle.digest('SHA-256',content));
export const attachmentHex=bytes=>Array.from(bytes,byte=>byte.toString(16).padStart(2,'0')).join('');
export function validateDraftAttachmentRows(rows){
 if(!Array.isArray(rows)||rows.length>MAX_TRACK_ATTACHMENTS)fail('ATTACHMENT_LIMIT');
 const ids=new Set(),references=new Set();let total=0;
 for(const row of rows){
  if(!row||typeof row.id!=='string'||!/^[A-Za-z0-9_-]{1,48}$/.test(row.id)||ids.has(row.id.toLowerCase()))fail('ATTACHMENT_ID');
  if(!attachmentReferenceValid(row.reference)||references.has(row.reference))fail('ATTACHMENT_REFERENCE');
  if(typeof row.mime!=='string'||row.mime.length>96||!/^[A-Za-z0-9!#$&^_.+-]+\/[A-Za-z0-9!#$&^_.+-]+$/.test(row.mime)||
   !Number.isInteger(row.role)||row.role<0||row.role>3||typeof row.required!=='boolean')fail('ATTACHMENT_FIELDS');
  if(!(row.content instanceof Uint8Array)||!row.content.length||row.content.length>((row.role!==3||/^image\//i.test(row.mime))?4194304:8388608))fail('ATTACHMENT_SIZE');
  total+=row.content.length;if(total>MAX_TRACK_PACKAGE_BYTES)fail('ATTACHMENT_LIMIT');
  ids.add(row.id.toLowerCase());references.add(row.reference);
 }
 return total;
}
export async function freezeDraftAttachmentRows(rows){
 validateDraftAttachmentRows(rows);
 const frozen=rows.map(row=>({id:row.id,reference:row.reference,mime:row.mime,role:row.role,required:row.required,content:row.content.slice()}));
 for(const row of frozen)row.hash=attachmentHex(await attachmentDigest(row.content));
 return frozen;
}
export async function buildDraftAttachmentIndex(source,rows,template){
 if(!(source instanceof Uint8Array)||!source.length||source.length>8388608)fail('ATTACHMENT_KML');
 const frozen=await freezeDraftAttachmentRows(rows),hash=await attachmentDigest(source);let entries;
 if(template){
  const original=decodeTrackAttachmentIndex(template),seen=new Set();entries=original[3];
  for(const row of frozen){const entry=entries.find(value=>value[0]===row.id);if(!entry||entry[2]!==row.content.length||!equal(entry[1],await attachmentDigest(row.content))||entry[3]!==row.mime||entry[4]!==row.reference||entry[5]!==row.role||Boolean(entry[6])!==row.required)fail('ATTACHMENT_ORIGINAL_MISMATCH');seen.add(row.id);}
  if(entries.some(entry=>entry[6]&&!seen.has(entry[0])))fail('ATTACHMENT_ORIGINAL_MISSING');
 }else entries=frozen.map(row=>[row.id,Uint8Array.from(row.hash.match(/../g),byte=>parseInt(byte,16)),row.content.length,row.mime,row.reference,row.role,row.required?1:0]);
 if(!entries.length)return null;
 const original=template?decodeTrackAttachmentIndex(template):null;
 const index=original&&original[2]===source.length&&equal(original[1],hash)?template.slice():encodeTrackAttachmentIndex([1,hash,source.length,entries]);
 return {attachmentIndex:index,attachmentFiles:frozen.map(row=>({id:row.id,content:row.content})),
  assets:[await attachmentDigest(index),index.length,entries.length,1]};
}
