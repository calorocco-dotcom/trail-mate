import {integer,validText,decode} from '../../geocaching/src/protocol.js';
import {validateTrackManifest} from './manifest-schema.js';
const invalid=()=>{throw Object.assign(Error('Invalid track activity or tags'),{code:'DRAFT_METADATA_INVALID'});};
export function validateTrackEditorMetadata(value){
 if(!value||Array.isArray(value)||Object.keys(value).sort().join(',')!=='activity,tags'||!integer(value.activity,0,255)||
  !Array.isArray(value.tags)||value.tags.length>8||value.tags.some(tag=>!validText(tag,48,false,true))||new Set(value.tags).size!==value.tags.length)invalid();
 return {activity:value.activity,tags:value.tags.slice()};
}
export function draftTrackEditorMetadata(snapshot){
 if(snapshot.editorMetadata!==undefined)return validateTrackEditorMetadata(snapshot.editorMetadata);
 const raw=snapshot.predecessor??snapshot.sourceProof;
 if(raw){const signed=decode(new Uint8Array(raw),4352),record=validateTrackManifest(decode(signed[0],4096));
  if(record[0]===2)return validateTrackEditorMetadata({activity:record[14][0],tags:record[14][1]});}
 return {activity:0,tags:[]};
}
