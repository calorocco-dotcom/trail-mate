import {MODULE_SCHEMA} from './meshtastic-module-schema.js';
import {readProtobufMessage,protobufVarintField,protobufBytesField,patchSingularProtobufField,editRepeatedProtobufMessage} from './module-protobuf-wire.js';
import {readMeshtasticModule,replaceMeshtasticModule} from './meshtastic-module-document.js';
const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true});
const fail=code=>{throw Object.assign(Error(code),{code});};
const join=parts=>Uint8Array.from(parts.flatMap(part=>[...part]));
const definition=name=>MODULE_SCHEMA.messages[name]??fail('MODULE_SCHEMA_UNKNOWN');
function scalar(field,frame){
 if(!frame)return field.type==='STRING'?'':field.type==='BOOL'?false:0;
 if(frame.wire!==(field.type==='STRING'?2:0))fail('MODULE_FIELD_WIRE');
 if(field.type==='STRING'){let value;try{value=decoder.decode(frame.value);}catch{fail('MODULE_FIELD_UTF8');}if(value.includes('\0')||frame.value.length>field.maxBytes)fail('MODULE_FIELD_STRING');return value;}
 if(field.type==='BOOL')return frame.value!==0n;
 if(field.type==='INT32'){if(frame.value>0xffffffffn&&frame.value<0xffffffff80000000n)fail('MODULE_FIELD_RANGE');return Number(BigInt.asIntN(32,frame.value));}
 if(frame.value>BigInt(field.max??0xffffffff))fail('MODULE_FIELD_RANGE');return Number(frame.value);
}
export function readStructuredModule(bytes,message=MODULE_SCHEMA.root){
 const schema=definition(message),frames=readProtobufMessage(bytes),fields=schema.fields.map(field=>{
  const matching=frames.filter(frame=>frame.number===field.number);
  let value;
  if(field.type==='MESSAGE'){
   if(matching.some(frame=>frame.wire!==2))fail('MODULE_FIELD_WIRE');
   if(field.cardinality==='REPEATED'){if(matching.length>field.maxCount)fail('MODULE_FIELD_COUNT');value=matching.map(frame=>readStructuredModule(frame.value,field.message));}
   else value=matching.length?readStructuredModule(join(matching.map(frame=>frame.value)),field.message):undefined;
  }else{for(const frame of matching)scalar(field,frame);value=scalar(field,matching.at(-1));}
  return {...field,present:matching.length>0,value};
 });
 return {message,fields,unknown:frames.filter(frame=>!schema.fields.some(field=>field.number===frame.number)).map(frame=>frame.raw)};
}
function encodeField(field,value){
 if(field.type==='BOOL'){if(typeof value!=='boolean')fail('MODULE_FIELD_BOOLEAN');return protobufVarintField(field.number,value?1:0);}
 if(field.type==='STRING'){
  if(typeof value!=='string'||value.includes('\0'))fail('MODULE_FIELD_STRING');const bytes=encoder.encode(value);
  if(decoder.decode(bytes)!==value||bytes.length>field.maxBytes)fail('MODULE_FIELD_STRING');return protobufBytesField(field.number,bytes);
 }
 if(!Number.isSafeInteger(value))fail('MODULE_FIELD_INTEGER');
 if(field.type==='UENUM'){if(!field.options.some(option=>option.value===value))fail('MODULE_FIELD_ENUM');}
 else if(value<(field.min??0)||value>field.max)fail('MODULE_FIELD_RANGE');
 return protobufVarintField(field.number,field.type==='INT32'?BigInt.asUintN(64,BigInt(value)):value);
}
export function updateStructuredModuleField(document,path,value){
 if(!Array.isArray(path)||!path.length||path.length>16)fail('MODULE_FIELD_PATH');
 let message=MODULE_SCHEMA.root,leaf;const numbers=[];
 for(let index=0;index<path.length;index++){
  leaf=definition(message).fields.find(field=>field.name===path[index]);if(!leaf)fail('MODULE_FIELD_UNKNOWN');
  if(leaf.readOnly)fail('MODULE_FIELD_READ_ONLY');if(leaf.cardinality==='REPEATED')fail('MODULE_FIELD_REPEATED');numbers.push(leaf.number);
  if(index<path.length-1){if(leaf.type!=='MESSAGE')fail('MODULE_FIELD_PATH');message=leaf.message;}
 }
 if(leaf.type==='MESSAGE')fail('MODULE_FIELD_MESSAGE');
 const bytes=readMeshtasticModule(document),encoded=encodeField(leaf,value),updated=patchSingularProtobufField(bytes,numbers,encoded);
 return replaceMeshtasticModule(document,updated);
}

export function setStructuredModulePresence(document,path,present){
 if(typeof present!=='boolean'||!Array.isArray(path)||!path.length||path.length>16)fail('MODULE_FIELD_PATH');
 let message=MODULE_SCHEMA.root,leaf;const numbers=[];
 for(let index=0;index<path.length;index++){
  leaf=definition(message).fields.find(field=>field.name===path[index]);
  if(!leaf||leaf.type!=='MESSAGE'||leaf.cardinality!=='OPTIONAL'||leaf.readOnly)fail('MODULE_FIELD_PATH');
  numbers.push(leaf.number);message=leaf.message;
 }
 const bytes=readMeshtasticModule(document);
 // Adding an existing optional message must retain its values and unknown bytes.
 let current=readStructuredModule(bytes);
 for(const name of path){current=current?.fields.find(field=>field.name===name)?.value;}
 if(present&&current)return replaceMeshtasticModule(document,bytes);
 // Removing an absent nested message must not create an empty ancestor.
 if(!present&&!current)return replaceMeshtasticModule(document,bytes);
 return replaceMeshtasticModule(document,patchSingularProtobufField(bytes,numbers,present?protobufBytesField(leaf.number,new Uint8Array()):null));
}

export function editStructuredModuleRow(document,path,index,{action='update',values={}}={}){
 if(!Array.isArray(path)||!path.length||path.length>16)fail('MODULE_FIELD_PATH');
 let message=MODULE_SCHEMA.root,leaf;const numbers=[];
 for(let position=0;position<path.length;position++){
  leaf=definition(message).fields.find(field=>field.name===path[position]);if(!leaf||leaf.type!=='MESSAGE'||leaf.readOnly)fail('MODULE_FIELD_PATH');
  if(position<path.length-1&&leaf.cardinality==='REPEATED')fail('MODULE_FIELD_PATH');numbers.push(leaf.number);message=leaf.message;
 }
 if(leaf.cardinality!=='REPEATED')fail('MODULE_FIELD_REPEATED');
 const changed=editRepeatedProtobufMessage(readMeshtasticModule(document),numbers,{action,index,maxCount:leaf.maxCount,edit:bytes=>{
  for(const [name,value] of Object.entries(values)){
   const field=definition(message).fields.find(item=>item.name===name);if(!field||field.type==='MESSAGE'||field.cardinality==='REPEATED'||field.readOnly)fail('MODULE_FIELD_UNKNOWN');
   bytes=patchSingularProtobufField(bytes,[field.number],encodeField(field,value));
  }
  return bytes;
 }});
 return replaceMeshtasticModule(document,changed);
}
