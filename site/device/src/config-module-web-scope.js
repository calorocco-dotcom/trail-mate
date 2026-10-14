import {isWebConfigurationField} from './config-web-scope.js';
import {MODULE_SCHEMA} from './meshtastic-module-schema.js';
import {readMeshtasticModule,replaceMeshtasticModule,MODULE_LIMIT} from './meshtastic-module-document.js';
import {readStructuredModule} from './meshtastic-module-model.js';
import {readProtobufMessage,protobufBytesField} from './module-protobuf-wire.js';
const join=parts=>Uint8Array.from(parts.flatMap(part=>[...part]));
const equal=(a,b)=>a.length===b.length&&a.every((value,index)=>value===b[index]);
export function isWebModuleField(path){return Array.isArray(path)&&path.length>0&&isWebConfigurationField(path.join('.'));}
function excludedPaths(message=MODULE_SCHEMA.root,names=[],numbers=[],result=[]){for(const field of MODULE_SCHEMA.messages[message].fields){const next=[...names,field.name],path=[...numbers,field.number];if(!isWebModuleField(next))result.push(path);else if(field.type==='MESSAGE'&&field.cardinality!=='REPEATED')excludedPaths(field.message,next,path,result);}return result;}
function preserve(bytes,original,path){
 const frames=readProtobufMessage(bytes),source=readProtobufMessage(original),number=path[0],before=source.filter(frame=>frame.number===number),after=frames.filter(frame=>frame.number===number);
 let replacement;
 if(path.length===1){if(equal(join(before.map(frame=>frame.raw)),join(after.map(frame=>frame.raw))))return bytes;replacement=before.map(frame=>frame.raw);}
 else{
  if([...before,...after].some(frame=>frame.wire!==2))throw Object.assign(Error('MODULE_FIELD_WIRE'),{code:'MODULE_FIELD_WIRE'});
  const candidate=join(after.map(frame=>frame.value)),retained=preserve(candidate,join(before.map(frame=>frame.value)),path.slice(1));if(equal(candidate,retained))return bytes;
  replacement=[protobufBytesField(number,retained)];
 }
 const parts=[];let placed=false;for(const frame of frames){if(frame.number!==number)parts.push(frame.raw);else if(!placed){parts.push(...replacement);placed=true;}}if(!placed)parts.push(...replacement);return join(parts);
}
// Preserve hidden compatibility fields during a known-format backup restore.
// Unknown formats remain opaque; this does not reinterpret future protobufs.
export function preserveExcludedModuleConfiguration(current,candidate){
 const format=document=>{try{return document.get('mt_module.format').value;}catch{return undefined;}};
 if(format(current)!==MODULE_LIMIT.format||format(candidate)!==MODULE_LIMIT.format)return {document:candidate,changed:false};
 const original=readMeshtasticModule(current),bytes=readMeshtasticModule(candidate);readStructuredModule(original);
 let retained=bytes;for(const path of excludedPaths())retained=preserve(retained,original,path);
 readStructuredModule(retained);
 return equal(bytes,retained)?{document:candidate,changed:false}:{document:replaceMeshtasticModule(candidate,retained),changed:true};
}
