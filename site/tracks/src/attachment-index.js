import {Identity} from '@reticulum/core';
import {encode,decode,bytes,integer,equal,validText} from '../../geocaching/src/protocol.js';
import {MAX_ATTACHMENT_INDEX_BYTES,MAX_TRACK_PACKAGE_BYTES,MAX_TRACK_ATTACHMENTS} from './manifest-extensions.js';
import {validateTrackManifest} from './manifest-schema.js';
export function attachmentReferenceValid(reference){
 if(!validText(reference,4096,false,true))return false;
 const match=/^https?:\/\//i.exec(reference);
 if(match){const host=reference.slice(match[0].length).split(/[/?#]/,1)[0];return !!host&&!host.includes('@')&&![...reference].some(c=>c==='\\'||c.codePointAt(0)<=32);}
 const path=reference.split(/[?#]/,1)[0];
 if(!path||path[0]==='/'||path[0]==='\\')return false;
 for(const part of path.split('/')){
  if(!part)return false;let size=0,dots=true;
  for(let i=0;i<part.length;i++){
   let c=part[i],code=c.charCodeAt(0);
   if(c==='%'){const hex=part.slice(i+1,i+3);if(hex.length!==2||!/^[0-9a-f]{2}$/i.test(hex))return false;code=parseInt(hex,16);c=String.fromCharCode(code);i+=2;if(c==='/')return false;}
   if(c==='\\'||c===':'||code<32||code===127)return false;size++;if(c!=='.')dots=false;
  }
  if(dots&&size<=2)return false;
 }
 return true;
}
export function validateTrackAttachmentIndex(record){
 if(!Array.isArray(record)||record.length!==4||record[0]!==1||!bytes(record[1],32)||!integer(record[2],1,8388608)||
  !Array.isArray(record[3])||!integer(record[3].length,1,MAX_TRACK_ATTACHMENTS))throw Error('轨迹附件索引无效');
 const ids=new Set(),references=new Set();let total=record[2];
 for(const entry of record[3]){
  if(!Array.isArray(entry)||entry.length!==7||typeof entry[0]!=='string'||!/^[A-Za-z0-9_-]{1,48}$/.test(entry[0])||!bytes(entry[1],32)||
   !integer(entry[2],1,8388608)||!validText(entry[3],96,false,true)||!/^[A-Za-z0-9!#$&^_.+-]+\/[A-Za-z0-9!#$&^_.+-]+$/.test(entry[3])||
   !attachmentReferenceValid(entry[4])||!integer(entry[5],0,3)||!integer(entry[6],0,1)||
   (entry[5]!==3||/^image\//i.test(entry[3]))&&entry[2]>4194304)throw Error('轨迹附件条目无效');
  const id=entry[0].toLowerCase();if(ids.has(id)||references.has(entry[4]))throw Error('轨迹附件引用冲突');ids.add(id);references.add(entry[4]);
  total+=entry[2];if(total>MAX_TRACK_PACKAGE_BYTES)throw Error('轨迹附件包超过容量限制');
 }
 return record;
}
export function encodeTrackAttachmentIndex(record){
 const raw=encode(validateTrackAttachmentIndex(record));if(raw.length>MAX_ATTACHMENT_INDEX_BYTES||record[2]+record[3].reduce((sum,row)=>sum+row[2],0)+raw.length>MAX_TRACK_PACKAGE_BYTES)throw Error('轨迹附件包超过容量限制');return raw;
}
export function decodeTrackAttachmentIndex(raw){
 const record=validateTrackAttachmentIndex(decode(raw,MAX_ATTACHMENT_INDEX_BYTES));
 if(record[2]+record[3].reduce((sum,row)=>sum+row[2],0)+raw.length>MAX_TRACK_PACKAGE_BYTES)throw Error('轨迹附件包超过容量限制');return record;
}
// Integrity binding only. Caller must independently verify author signature and
// directory currentness, then verify every actual resource before installation.
export async function matchTrackAttachmentIndex(index,manifest){
 if(!(index instanceof Uint8Array)||!(manifest instanceof Uint8Array))throw Error('轨迹附件索引绑定无效');
 const content=index.slice(),signedBytes=manifest.slice(),record=validateTrackManifest(decode(signedBytes,4096)),assets=record[0]===2?record[15]:null;
 const decoded=decodeTrackAttachmentIndex(content);
 if(!assets||assets[1]!==content.length||assets[2]!==decoded[3].length||record[13][1]!==decoded[2]||!equal(record[13][0],decoded[1]))throw Error('轨迹附件索引绑定无效');
 const hash=await Identity.fullHash(content);if(!equal(hash,assets[0]))throw Error('轨迹附件索引摘要不匹配');
 return Object.freeze({index:content,record:decoded,hash:hash.slice()});
}
