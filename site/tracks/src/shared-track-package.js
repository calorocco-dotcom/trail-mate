import {Identity} from '@reticulum/core';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {readSharedTrack} from './shared-track-read.js';
import {matchTrackAttachmentIndex} from './attachment-index.js';
import {validateTrackPackageStatus} from './package-status.js';
import {trackShareLink} from './shared-id.js';
import {TRACK_BULK_CHUNK_BYTES} from './bulk-file-profile.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};
async function readFile(request,hash,total,{signal,chunkBytes,onProgress,phase,id}){
 if(!bytes(hash,32)||!integer(total,1,8388608))fail('TRACK_PACKAGE_SIZE');
 const content=new Uint8Array(total);let offset=0;
 while(offset<total){check(signal);const count=Math.min(chunkBytes,total-offset),reply=await request(8,[hash.slice(),offset,count],{signal});check(signal);
  if(!Array.isArray(reply)||reply.length!==4||!bytes(reply[0],32)||!equal(reply[0],hash)||reply[1]!==total||reply[2]!==offset||!(reply[3] instanceof Uint8Array)||reply[3].length!==count)fail('TRACK_PACKAGE_FILE_RESPONSE');
  content.set(reply[3],offset);offset+=count;onProgress?.({phase,id,offset,total});
 }
 if(!equal(await Identity.fullHash(content),hash))fail('TRACK_PACKAGE_HASH');check(signal);return content;
}
// Only a fully checked local file is marked verified. Directory availability
// is separate: an optional absent file is not interchangeable with bad bytes.
export async function readSharedTrackPackage(request,reference,{baseUrl,signal,chunkBytes=1024,maxBytes=8388608,packageSupported=false,onProgress}={}){
 check(signal);
 if(!integer(chunkBytes,1,TRACK_BULK_CHUNK_BYTES))fail('TRACK_READ_OPTIONS_INVALID');
 const summary=await readSharedTrack(request,reference,{baseUrl,signal,maxBytes,chunkBytes,download:false});check(signal);
 const descriptor=summary.record[0]===2?summary.record[15]:null;
 if(descriptor&&!packageSupported)fail('TRACK_PACKAGE_UNSUPPORTED');
 if(summary.record[13][1]>maxBytes)fail('TRACK_FILE_TOO_LARGE');
 const source=await readFile(request,summary.record[13][0],summary.record[13][1],{signal,chunkBytes,onProgress,phase:'kml'}),detail={...summary,source};
 if(!descriptor)return {detail,attachmentIndex:null,index:null,files:[],rows:[],ready:true,allComplete:true,availability:null};
 const signed=[detail.manifest.slice(),detail.signature.slice()],status=await request(14,[signed],{signal});check(signal);
 if(!Array.isArray(status)||status.length!==6||status[0]!==1||!bytes(status[1],32)||!equal(status[1],detail.revisionHash)||![status[2],status[3],status[4]].every(value=>integer(value,0,1)))fail('TRACK_PACKAGE_STATUS');
 if(status[3]===0){const availability=validateTrackPackageStatus(status,detail.revisionHash,null);return {detail,attachmentIndex:null,index:null,files:[],rows:[],ready:false,allComplete:false,availability};}
 const attachmentIndex=await readFile(request,descriptor[0],descriptor[1],{signal,chunkBytes,onProgress,phase:'index'});
 const {record:index}=await matchTrackAttachmentIndex(attachmentIndex,detail.manifest),availability=validateTrackPackageStatus(status,detail.revisionHash,index);
 const files=[],rows=[],byHash=new Map();
 for(let i=0;i<index[3].length;i++){
  check(signal);const entry=index[3][i],row={id:entry[0],hash:entry[1].slice(),bytes:entry[2],mime:entry[3],reference:entry[4],role:entry[5],required:entry[6]===1,verified:false};
  if(availability.rows[i].available){
   const key=Array.from(entry[1],byte=>byte.toString(16).padStart(2,'0')).join('')+':'+entry[2];
   let content=byHash.get(key);
   if(!content){content=await readFile(request,entry[1],entry[2],{signal,chunkBytes,onProgress:event=>onProgress?.({...event,reference:entry[4]}),phase:'resource',id:entry[0]});byHash.set(key,content);}
   files.push({id:entry[0],content});row.verified=true;
  }
  rows.push(row);
 }
 // Recheck currentness after a potentially long download, without changing
 // the exact signed version the user selected or downloading source again.
 const pinned=trackShareLink(baseUrl??'https://trailmate.invalid/',detail.objectId,{revision:detail.revisionHash});
 const latest=await readSharedTrack(request,pinned,{signal,download:false,baseUrl,maxBytes,chunkBytes});check(signal);
 if(!equal(latest.manifest,detail.manifest)||!equal(latest.signature,detail.signature))fail('TRACK_PACKAGE_CHANGED');
 return {detail:{...detail,current:latest.current},attachmentIndex,index,files,rows,availability,
  ready:rows.every(row=>!row.required||row.verified),allComplete:rows.every(row=>row.verified)};
}
