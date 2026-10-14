import {bytes,integer,validText} from '../../geocaching/src/protocol.js';
export const MAX_ATTACHMENT_INDEX_BYTES=294912,MAX_TRACK_PACKAGE_BYTES=64*1024*1024,MAX_TRACK_ATTACHMENTS=64;
export function validateTrackMetadata(metadata,points){
 if(!Array.isArray(metadata)||metadata.length!==8||!integer(metadata[0],0,255)||!Array.isArray(metadata[1])||metadata[1].length>8||
  metadata[1].some(tag=>!validText(tag,48,false,true))||new Set(metadata[1]).size!==metadata[1].length||
  !integer(metadata[2],0,0xffffffff)||!integer(metadata[3],0,points)||!integer(metadata[4],0,points)||!integer(metadata[5],0,2)||!integer(metadata[6],0,4)||
  metadata[7]!==null&&(!Array.isArray(metadata[7])||metadata[7].length!==2||!bytes(metadata[7][0],32)||!bytes(metadata[7][1],32)))throw Error('轨迹扩展元数据无效');
 return metadata;
}
export function validateAttachmentIndexDescriptor(index){
 if(index===null)return null;
 if(!Array.isArray(index)||index.length!==4||!bytes(index[0],32)||!integer(index[1],1,MAX_ATTACHMENT_INDEX_BYTES)||!integer(index[2],1,MAX_TRACK_ATTACHMENTS)||index[3]!==1)throw Error('轨迹附件索引绑定无效');
 return index;
}
