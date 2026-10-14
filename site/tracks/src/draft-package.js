import {encode,decode,equal,bytes} from '../../geocaching/src/protocol.js';
import {parseKml} from './kml.js';
import {summarizeKml} from './kml-summary.js';
import {buildDraftAttachmentIndex,attachmentDigest} from './draft-attachments.js';
import {matchTrackAttachmentIndex} from './attachment-index.js';
import {retainTrackAttachmentBindings} from './draft-store.js';
import {draftTrackEditorMetadata} from './track-editor-metadata.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
export async function draftPackageCreationOptions(snapshot){
 const packet=await buildDraftAttachmentIndex(snapshot.source,snapshot.attachmentRows??[],snapshot.attachmentTemplate?new Uint8Array(snapshot.attachmentTemplate):undefined);
 const previous=snapshot.predecessor?decode(decode(new Uint8Array(snapshot.predecessor),4352)[0],4096):null;
 if(previous?.[0]===2&&previous[15]&&!packet&&snapshot.attachmentCount===undefined)fail('ATTACHMENT_ORIGINAL_MISSING');
 if(snapshot.archive&&previous?.[0]===2&&previous[15]&&!packet)fail('ATTACHMENT_ORIGINAL_MISSING');
 const origin=snapshot.sourceProof?decode(decode(new Uint8Array(snapshot.sourceProof),4352)[0],4096):null;
 if(!packet&&previous?.[0]!==2&&!origin&&snapshot.editorMetadata===undefined)return {};
 const model=parseKml(snapshot.source),summary=summarizeKml(model);
 const waypoints=geometry=>geometry.type==='Point'?geometry.points.length:(geometry.geometries??[]).reduce((count,child)=>count+waypoints(child),0);
 const inherited=previous?.[0]===2?previous[14]:origin?.[0]===2?origin[14]:[0,[],0,0,0,0,0,null];
 const {verifyTrackManifest}=await import('./manifest.js');
 const proof=origin&&!previous?await verifyTrackManifest(decode(new Uint8Array(snapshot.sourceProof),4352)):null;
 const metadata=snapshot.archive?previous[14]:[...inherited];
 if(!snapshot.archive){const edited=draftTrackEditorMetadata(snapshot);metadata[0]=edited.activity;metadata[1]=edited.tags;metadata[3]=summary.segments;metadata[4]=model.features.reduce((count,feature)=>count+feature.geometries.reduce((sum,geometry)=>sum+waypoints(geometry),0),0);if(proof)metadata[7]=[proof.objectId,proof.revisionHash];}
 return {assets:packet?.assets??null,metadata};
}
export async function draftPackageInputs(snapshot,plan,{persist=true}={}){
 const packet=await buildDraftAttachmentIndex(snapshot.source,snapshot.attachmentRows??[],snapshot.attachmentTemplate?new Uint8Array(snapshot.attachmentTemplate):undefined),record=decode(plan.manifest,4096);
 if(!packet){if(record[0]===2&&record[15])fail('ATTACHMENT_ORIGINAL_MISSING');return {};}
 await matchTrackAttachmentIndex(packet.attachmentIndex,plan.manifest);
 if(!persist)return packet;
 const manifestHash=await attachmentDigest(plan.manifest),random=()=>crypto.getRandomValues(new Uint8Array(16));
 let raw=snapshot.attachmentBindings?new Uint8Array(snapshot.attachmentBindings):null;
 if(!raw)raw=await retainTrackAttachmentBindings(snapshot.id,snapshot.version,plan.manifest,encode([1,manifestHash,random(),random(),packet.attachmentFiles.map(row=>[row.id,random()])]));
 const binding=decode(raw,6144),ids=new Set([plan.kmlUploadId,plan.manifestUploadId].map(value=>Array.from(value).join(',')));
 if(!Array.isArray(binding)||binding.length!==5||binding[0]!==1||!equal(binding[1],manifestHash)||!Array.isArray(binding[4])||binding[4].length!==packet.attachmentFiles.length)fail('ATTACHMENT_BINDING');
 for(const value of [binding[2],binding[3],...binding[4].map(row=>row?.[1])]){
  const key=Array.from(value??[]).join(',');if(!bytes(value,16)||!value.some(Boolean)||ids.has(key))fail('ATTACHMENT_BINDING');ids.add(key);
 }
 if(binding[4].some((row,index)=>!Array.isArray(row)||row.length!==2||row[0]!==packet.attachmentFiles[index].id))fail('ATTACHMENT_BINDING');
 return {...packet,indexUploadId:binding[2],packageUploadId:binding[3],attachmentUploadIds:binding[4].map(row=>({id:row[0],uploadId:row[1]}))};
}
