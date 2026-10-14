import {Destination,DestType,toHex} from '@reticulum/core';
import {DirectoryClient} from '../../geocaching/src/protocol-client.js';
import {bytes,decode,equal,integer,validText} from '../../geocaching/src/protocol.js';
import {querySharedTrackPage} from './shared-track-query.js';
import {readSharedTrack} from './shared-track-read.js';
import {publishIPTrack} from './ip-publication.js';
import {createTrackRegionClient} from './region-client.js';
import {freezeTrackAttachmentInputs} from './attachment-inputs.js';
import {trackDirectoryError} from './directory-error.js';
import {TRACK_BULK_OPERATION,TRACK_BULK_CHUNK_BYTES} from './bulk-file-profile.js';

export class TrackDirectoryClient extends DirectoryClient{
 constructor(notify=()=>{}){super(notify,'trailmate.track.v1');}
 async request(directory,operation,body,options){
  try{return await super.request(directory,operation,body,options);}
  catch(error){throw trackDirectoryError(error);}
 }
 async discover({destinationHash,identity,appData,packet}){
  if(this.closed||!(appData instanceof Uint8Array)||appData.length>160)return;
  const service=await Destination.OUT('trailmate.track.directory',DestType.SINGLE,identity,this.rns);
  if(!equal(service.destinationHash,destinationHash))return;
  const data=decode(appData,160),delivery=await Destination.OUT('lxmf.delivery',DestType.SINGLE,identity,this.rns);
  if(!Array.isArray(data)||data.length!==5||data[0]!==1||data[1]!==this.appType||!bytes(data[2],16)||!equal(data[2],delivery.destinationHash)||
     !bytes(data[3],16)||!validText(data[4],40,false,true))return;
  const key=toHex(data[2]);if(this.directories.has(key)||this.directories.size>=32)return;
  const entry={key,destination:data[2].slice(),identity,epoch:data[3].slice(),name:data[4],ready:false};this.directories.set(key,entry);
  try{await this.rns.transport.rememberIdentity(await packet.getHash(),entry.destination,await identity.getPublicKey(),null);await this.enqueue(()=>this.checkDirectory(entry));}
  catch(error){if(this.directories.get(key)===entry&&!entry.ready)this.directories.delete(key);this.notify({type:'source-error',name:entry.name,message:error.message});}
 }
 async checkDirectory(entry){
  entry.ready=false;
  const expectedEpoch=entry.epoch.slice();
  const caps=await this.request(entry,0,[]),publicKey=await entry.identity.getPublicKey();
  if(!Array.isArray(caps)||caps.length!==8||caps[0]!==1||caps[1]!==this.appType||!equal(caps[2],publicKey)||!equal(caps[3],entry.epoch)||
     caps[4]!==8388608||caps[5]!==1024||!integer(caps[6],1,32)||!Array.isArray(caps[7])||![0,1,2,3,4,5,6,7,8].every(op=>caps[7].includes(op)))throw Error('Track directory is not compatible');
  if(this.closed||this.directories.get(entry.key)!==entry||!equal(entry.epoch,expectedEpoch))throw Error('Track directory connection changed');
  entry.operations=caps[7].slice();
  if(!entry.operations.includes(14)||!entry.operations.includes(15)){
   let extended;
   try{extended=await this.request(entry,0,[2]);}
   catch(error){if(error.status!==422)throw error;}
   if(extended!==undefined){
    if(!Array.isArray(extended)||extended.length!==8||extended[0]!==1||extended[1]!==this.appType||!equal(extended[2],publicKey)||!equal(extended[3],expectedEpoch)||
     extended[4]!==8388608||extended[5]!==1024||!integer(extended[6],1,32)||!Array.isArray(extended[7])||![0,1,2,3,4,5,6,7,8].every(op=>extended[7].includes(op)))throw Error('Track directory is not compatible');
    entry.operations=extended[7].slice();
   }
  }
  if(this.closed||this.directories.get(entry.key)!==entry||!equal(entry.epoch,expectedEpoch))throw Error('Track directory connection changed');
  entry.ready=true;this.notify({type:'sources',count:[...this.directories.values()].filter(row=>row.ready).length});
 }
 queryPage(directoryKey,options={}){
  const entry=this.directories.get(directoryKey);if(!entry?.ready)return Promise.reject(Error('Verified track directory unavailable'));
  if((options.activity!=null||Array.isArray(options.tags)&&options.tags.length)&&!entry.operations?.includes(15))return Promise.reject(Object.assign(Error('Directory metadata query unavailable'),{code:'TRACK_QUERY_METADATA_UNSUPPORTED'}));
  return this.enqueue(()=>querySharedTrackPage((op,body)=>{
   if(this.closed||this.directories.get(directoryKey)!==entry||!entry.ready)throw Error('Track directory connection changed');
   return this.request(entry,op,body);
  },options));
 }
 readTrack(directoryKey,reference,options={}){
  const entry=this.directories.get(directoryKey);if(!entry?.ready)return Promise.reject(Error('Verified track directory unavailable'));
  const bulkFiles=entry.operations?.includes(TRACK_BULK_OPERATION)===true;
  return this.enqueue(()=>readSharedTrack((op,body)=>{
   if(this.closed||this.directories.get(directoryKey)!==entry||!entry.ready)throw Error('Track directory connection changed');
   return this.request(entry,op===8&&bulkFiles?TRACK_BULK_OPERATION:op,op===8&&bulkFiles?[2,...body]:body);
  },reference,{...options,chunkBytes:options.chunkBytes??(bulkFiles?TRACK_BULK_CHUNK_BYTES:1024)}));
 }
 publishTrack(directoryKey,signed,source,options={}){
  const entry=this.directories.get(directoryKey);if(!entry?.ready)return Promise.reject(Error('Verified track directory unavailable'));
  let attachments;try{attachments=freezeTrackAttachmentInputs(options.attachmentIndex,options.attachmentFiles,source?.length??0);}catch(error){return Promise.reject(error);}
  const content=signed?.map(value=>value.slice()),file=source?.slice(),epoch=entry.epoch.slice(),settings={...options,authorPublicKey:options.authorPublicKey?.slice(),requestId:options.requestId?.slice(),
   packageSupported:entry.operations?.includes(14)===true,bulkFiles:entry.operations?.includes(TRACK_BULK_OPERATION)===true,attachmentIndex:attachments.index,attachmentFiles:attachments.files};
  return this.enqueue(async()=>{
   const publicKey=await entry.identity.getPublicKey();
   return publishIPTrack((op,body,requestOptions)=>{
    if(this.closed||this.directories.get(directoryKey)!==entry||!entry.ready||!equal(entry.epoch,epoch))throw Error('Track directory connection changed');
    return this.request(entry,op,body,requestOptions);
   },content,file,{...settings,directoryPublicKey:publicKey,directoryEpoch:epoch});
  });
 }
 regionContext(directoryKey,options={}){
  const entry=this.directories.get(directoryKey);if(!entry?.ready)return Promise.reject(Error('Verified track directory unavailable'));
  const epoch=entry.epoch.slice(),generation=this.generation;
  const request=(op,body,{signal}={})=>this.enqueue(async()=>{
   if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');
   if(this.closed||this.generation!==generation||this.directories.get(directoryKey)!==entry||!entry.ready||!equal(entry.epoch,epoch))throw Error('Track region directory connection changed');
   let response;
   try{response=await this.request(entry,op,body);}
   catch(error){if(error.status===422&&error.reason==='region capability unsupported')throw Object.assign(Error(error.message),{status:422,diagnostic:error.reason});throw error;}
   if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');
   if(this.closed||this.generation!==generation||this.directories.get(directoryKey)!==entry||!entry.ready||!equal(entry.epoch,epoch))throw Error('Track region directory connection changed');
   return response;
  });
  return createTrackRegionClient(request,{...options,metadataSupported:entry.operations?.includes(16)===true});
 }
}
