import {MODULE_LIMIT} from './meshtastic-module-document.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
const numberValid=value=>Number.isInteger(value)&&value>=1&&value<=0x1fffffff;
const join=parts=>{const size=parts.reduce((sum,value)=>sum+value.length,0);if(size>MODULE_LIMIT.bytes)fail('MODULE_PROTOBUF_LIMIT');const result=new Uint8Array(size);let offset=0;for(const part of parts){result.set(part,offset);offset+=part.length;}return result;};
function varint(value){
 if(typeof value==='number'){if(!Number.isSafeInteger(value))fail('MODULE_PROTOBUF_VALUE');value=BigInt(value);}
 if(typeof value!=='bigint'||value<0n||value>0xffffffffffffffffn)fail('MODULE_PROTOBUF_VALUE');
 const bytes=[];do{let byte=Number(value&127n);value>>=7n;if(value)byte|=128;bytes.push(byte);}while(value);return Uint8Array.from(bytes);
}
export function protobufVarintField(number,value){if(!numberValid(number))fail('MODULE_PROTOBUF_FIELD');return join([varint(BigInt(number)*8n),varint(value)]);}
export function protobufBytesField(number,value){if(!numberValid(number)||!(value instanceof Uint8Array))fail('MODULE_PROTOBUF_FIELD');return join([varint(BigInt(number)*8n+2n),varint(value.length),value]);}

// Unknown fields keep their exact tag/value bytes, including non-minimal
// varints. Message payloads are opaque until a known singular path is edited.
export function readProtobufMessage(bytes){
 if(!(bytes instanceof Uint8Array)||bytes.length>MODULE_LIMIT.bytes)fail('MODULE_PROTOBUF_LIMIT');
 let offset=0;
 const integer=()=>{let value=0n;for(let index=0;index<10;index++){if(offset>=bytes.length)fail('MODULE_PROTOBUF_TRUNCATED');const byte=bytes[offset++];if(index===9&&byte>1)fail('MODULE_PROTOBUF_OVERFLOW');value|=BigInt(byte&127)<<BigInt(index*7);if(!(byte&128))return value;}fail('MODULE_PROTOBUF_OVERFLOW');};
 const take=size=>{if(!Number.isSafeInteger(size)||size<0||size>bytes.length-offset)fail('MODULE_PROTOBUF_TRUNCATED');const value=bytes.slice(offset,offset+size);offset+=size;return value;};
 const scan=(end,depth)=>{
  if(depth>16)fail('MODULE_PROTOBUF_DEPTH');const fields=[];
  while(offset<bytes.length){const start=offset,tag=integer(),number=Number(tag>>3n),wire=Number(tag&7n);if(!numberValid(number))fail('MODULE_PROTOBUF_FIELD');
   if(wire===4){if(number!==end)fail('MODULE_PROTOBUF_GROUP');return fields;}
   let value;
   if(wire===0)value=integer();else if(wire===1)value=take(8);else if(wire===2){const size=integer();if(size>BigInt(bytes.length-offset))fail('MODULE_PROTOBUF_TRUNCATED');value=take(Number(size));}
   else if(wire===3)scan(number,depth+1);else if(wire===5)value=take(4);else fail('MODULE_PROTOBUF_WIRE');
   fields.push({number,wire,value,raw:bytes.slice(start,offset)});
  }
  if(end!==undefined)fail('MODULE_PROTOBUF_GROUP');return fields;
 };
 return scan(undefined,0);
}

// Singular scalar duplicates are replaced together. Duplicate singular message
// occurrences merge in wire order before editing, preserving protobuf merge
// semantics and every unedited child (known or unknown). Repeated fields need
// a separate schema-aware editor and must not call this singular-path API.
export function patchSingularProtobufField(bytes,path,replacement){
 if(!Array.isArray(path)||!path.length||path.length>16||!path.every(numberValid))fail('MODULE_PROTOBUF_PATH');
 const original=readProtobufMessage(bytes),number=path[0];
 let encoded;
 if(path.length===1){
  if(replacement!==null){const fields=readProtobufMessage(replacement);if(fields.length!==1||fields[0].number!==number)fail('MODULE_PROTOBUF_REPLACEMENT');encoded=replacement.slice();}
 }else{
  const parents=original.filter(field=>field.number===number);if(parents.some(field=>field.wire!==2))fail('MODULE_PROTOBUF_PARENT');
  const merged=join(parents.map(field=>field.value));
  const child=patchSingularProtobufField(merged,path.slice(1),replacement);encoded=protobufBytesField(number,child);
 }
 const parts=[];let replaced=false;
 for(const field of original){if(field.number===number){if(!replaced&&encoded)parts.push(encoded);replaced=true;}else parts.push(field.raw);}
 if(!replaced&&encoded)parts.push(encoded);return join(parts);
}

// Edits one repeated MESSAGE occurrence. Other rows keep their raw bytes and
// interleaved unrelated fields stay in place. Ancestors are singular messages.
export function editRepeatedProtobufMessage(bytes,path,{action,index,maxCount,edit}){
 if(!Array.isArray(path)||!path.length||path.length>16||!path.every(numberValid)||!['insert','update','remove'].includes(action)||!Number.isInteger(index)||index<0||!Number.isInteger(maxCount)||maxCount<1||maxCount>64)fail('MODULE_PROTOBUF_REPEATED');
 const fields=readProtobufMessage(bytes),number=path[0],matching=fields.filter(field=>field.number===number);
 if(matching.some(field=>field.wire!==2))fail('MODULE_PROTOBUF_PARENT');
 if(path.length>1){const changed=editRepeatedProtobufMessage(join(matching.map(field=>field.value)),path.slice(1),{action,index,maxCount,edit});return patchSingularProtobufField(bytes,[number],protobufBytesField(number,changed));}
 if(matching.length>maxCount||index>(action==='insert'?matching.length:matching.length-1)||action==='insert'&&matching.length===maxCount)fail('MODULE_PROTOBUF_REPEATED');
 let replacement;
 if(action!=='remove'){
  if(typeof edit!=='function')fail('MODULE_PROTOBUF_REPEATED');const payload=edit(action==='insert'?new Uint8Array():matching[index].value.slice());
  readProtobufMessage(payload);replacement=protobufBytesField(number,payload);
 }
 const parts=[];let ordinal=0,inserted=false;
 for(const field of fields){
  if(field.number!==number){parts.push(field.raw);continue;}
  if(action==='insert'&&ordinal===index){parts.push(replacement);inserted=true;}
  if(ordinal!==index||action==='insert')parts.push(field.raw);else if(action==='update')parts.push(replacement);
  ordinal++;
 }
 if(action==='insert'&&!inserted)parts.push(replacement);return join(parts);
}
