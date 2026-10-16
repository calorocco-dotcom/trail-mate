import {MAX_ATTACHMENT_INDEX_BYTES,MAX_TRACK_PACKAGE_BYTES,MAX_TRACK_ATTACHMENTS} from './manifest-extensions.js';
// Admission precedes copies so a selected oversized file cannot force a large
// allocation before the protocol budget is checked.
export function freezeTrackAttachmentInputs(index,files=[],kmlBytes=0){
 if(index!==undefined&&(!(index instanceof Uint8Array)||!index.length||index.length>MAX_ATTACHMENT_INDEX_BYTES)||
  !Array.isArray(files)||files.length>MAX_TRACK_ATTACHMENTS||files.some(row=>!row||typeof row.id!=='string'||!(row.content instanceof Uint8Array)||!row.content.length||row.content.length>8388608)||
  kmlBytes+(index?.length??0)+files.reduce((sum,row)=>sum+row.content.length,0)>MAX_TRACK_PACKAGE_BYTES)throw Error('Invalid track attachment input');
 return {index:index?.slice(),files:files.map(row=>({id:row.id,content:row.content.slice()}))};
}
