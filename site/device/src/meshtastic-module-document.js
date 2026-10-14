import {TmsDocument} from './tms-document.js';
// Matched to the checked-in LocalModuleConfig nanopb bound and TMS extension.
export const MODULE_LIMIT=Object.freeze({bytes:673,chunkBytes:240,chunks:3,format:'pb-v1'});
const fail=code=>{throw Object.assign(Error(code),{code});};
function records(document){
 try{return {format:document.get('mt_module.format'),size:document.get('mt_module.size'),count:document.get('mt_module.chunk_count'),chunks:Array.from({length:3},(_,index)=>document.get('mt_module.chunk.'+index))};}
 catch{fail('MODULE_RECORDS_MISSING');}
}
export function readMeshtasticModule(document){
 const record=records(document);
 if(record.format.type!=='str'||record.format.value!==MODULE_LIMIT.format||record.size.type!=='u16'||!Number.isInteger(record.size.value)||record.size.value<1||record.size.value>MODULE_LIMIT.bytes||record.count.type!=='u8'||record.count.value!==MODULE_LIMIT.chunks)fail('MODULE_FORMAT_INVALID');
 const result=new Uint8Array(record.size.value);
 let offset=0;
 for(const chunk of record.chunks){const expected=Math.min(MODULE_LIMIT.chunkBytes,Math.max(0,result.length-offset));if(chunk.type!=='b64'||!(chunk.value instanceof Uint8Array)||chunk.value.length!==expected)fail('MODULE_CHUNK_INVALID');result.set(chunk.value,offset);offset+=expected;}
 return result;
}
// Returns a new document. All metadata/chunks change together, or none do;
// unrelated known/unknown records and existing edits remain preserved.
// This layer validates the container, not protobuf semantics or device rules.
export function replaceMeshtasticModule(document,bytes){
 if(!(bytes instanceof Uint8Array)||bytes.length<1||bytes.length>MODULE_LIMIT.bytes)fail('MODULE_SIZE_INVALID');
 readMeshtasticModule(document);
 const updated=new TmsDocument(document.serialize());
 updated.set('mt_module.size',bytes.length);
 for(let index=0;index<MODULE_LIMIT.chunks;index++){const start=index*MODULE_LIMIT.chunkBytes;updated.set('mt_module.chunk.'+index,bytes.slice(start,Math.min(bytes.length,start+MODULE_LIMIT.chunkBytes)));}
 updated.serialize();return updated;
}
