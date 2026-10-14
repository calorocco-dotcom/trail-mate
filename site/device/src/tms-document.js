// Browser-side typed editor for the existing firmware TMSET7 document.
// The firmware remains the authority for required fields and platform constraints.
export const TMS_LIMIT = Object.freeze({documentBytes:32768,lineBytes:384,records:384,schema:7});
const utf8=new TextEncoder(), strictUtf8=new TextDecoder('utf-8',{fatal:true});
const ranges={u8:[0,255],i8:[-128,127],u16:[0,65535],i32:[-2147483648,2147483647],u32:[0,4294967295]};
const safe=/^[A-Za-z0-9_.\/:\-]$/;
function fail(message,key){const error=new Error(message);error.field=key;throw error;}
function parseValue(type,raw,key){
 if(type==='bool'){if(raw!=='0'&&raw!=='1')fail('Invalid boolean',key);return raw==='1';}
 if(ranges[type]){if(!/^-?\d+$/.test(raw))fail('Invalid integer',key);const n=Number(raw),[min,max]=ranges[type];if(!Number.isSafeInteger(n)||n<min||n>max)fail('Integer out of range',key);return n;}
 if(type==='f32'){if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(raw))fail('Invalid floating-point value',key);const n=Number(raw);if(!Number.isFinite(n)||!Number.isFinite(Math.fround(n)))fail('Float out of range',key);return n;}
 if(type==='enum'){if(!/^[A-Za-z0-9_.\-]+$/.test(raw))fail('Invalid enum',key);return raw;}
 if(type==='str'){
   const data=[];
   for(let i=0;i<raw.length;i++){if(raw[i]==='%'){if(!/^[0-9a-fA-F]{2}$/.test(raw.slice(i+1,i+3)))fail('Invalid text escape',key);data.push(parseInt(raw.slice(i+1,i+3),16));i+=2;}
     else {if(raw.charCodeAt(i)>127||!safe.test(raw[i]))fail('Unescaped text byte',key);data.push(raw.charCodeAt(i));}}
   if(data.includes(0))fail('Text contains NUL',key);
   try{return strictUtf8.decode(Uint8Array.from(data));}catch{fail('Invalid UTF-8',key);}
 }
 if(type==='b64'){
   if(!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(raw))fail('Invalid base64',key);
   const binary=atob(raw);if(btoa(binary)!==raw)fail('Noncanonical base64',key);return Uint8Array.from(binary,c=>c.charCodeAt(0));
 }
 // Future extension records remain byte-for-byte preserved, never editable.
 return undefined;
}
function encodeValue(type,value,key){
 if(type==='str'){
   if(typeof value!=='string'||value.includes('\0'))fail('Invalid text',key);
   return [...utf8.encode(value)].map(b=>safe.test(String.fromCharCode(b))?String.fromCharCode(b):'%'+b.toString(16).toUpperCase().padStart(2,'0')).join('');
 }
 if(type==='bool'){if(typeof value!=='boolean')fail('Expected boolean',key);return value?'1':'0';}
 if(type==='b64'){if(!(value instanceof Uint8Array))fail('Expected binary value',key);return btoa(Array.from(value,b=>String.fromCharCode(b)).join(''));}
 const raw=String(value);parseValue(type,raw,key);return raw;
}
function cloneValue(value){return value instanceof Uint8Array?value.slice():value;}
export class TmsDocument {
 #records;#original;
 constructor(bytes){
   if(!(bytes instanceof Uint8Array)||bytes.length>TMS_LIMIT.documentBytes)throw new Error('Invalid TMS document size');
   const text=strictUtf8.decode(bytes);if(text.includes('\0'))throw new Error('NUL in TMS document');
   const lines=text.split('\n');if(lines.at(-1)==='')lines.pop();
   const clean=lines.map(line=>line.endsWith('\r')?line.slice(0,-1):line);
   if(clean[0]!=='TMSET7'||clean.at(-1)!=='END')throw new Error('Unsupported or incomplete TMS document');
   this.#records=new Map();this.#original=bytes.slice();
   for(let i=1;i<clean.length-1;i++){
     const line=clean[i];if(utf8.encode(line).length>=TMS_LIMIT.lineBytes)throw new Error('TMS line exceeds firmware budget');
     const m=/^([A-Za-z0-9_.\-]+)=([A-Za-z0-9]+):(.*)$/.exec(line);if(!m)throw new Error('Malformed TMS record');
     const [,key,type,raw]=m;if(this.#records.has(key))fail('Duplicate TMS field',key);
     this.#records.set(key,{key,type,raw,value:parseValue(type,raw,key),changed:false});
   }
   if(this.#records.size>TMS_LIMIT.records)throw new Error('Too many TMS records');
   if(this.#records.get('schema.version')?.value!==7||this.#records.get('document.kind')?.type!=='enum'||!['working','backup'].includes(this.#records.get('document.kind')?.value))throw new Error('TMS header does not match schema');
 }
 get kind(){return this.#records.get('document.kind').value;}
 get size(){return this.#records.size;}
 get originalBytes(){return this.#original.slice();}
 entries(){return [...this.#records.values()].map(r=>({...r,value:cloneValue(r.value)}));}
 get(key){const r=this.#records.get(key);if(!r)fail('Unknown TMS field',key);return {...r,value:cloneValue(r.value)};}
 set(key,value){
   const r=this.#records.get(key);if(!r||r.value===undefined)fail('Unknown or unsupported TMS field',key);
   if(key==='schema.version'||key==='document.kind')fail('Header fields are read-only',key);
   const raw=encodeValue(r.type,value,key);if(utf8.encode(`${key}=${r.type}:${raw}`).length>=TMS_LIMIT.lineBytes)fail('Field exceeds TMS line budget',key);
   r.raw=raw;r.value=cloneValue(value);r.changed=true;
 }
 serialize({kind=this.kind}={}){
   if(!['working','backup'].includes(kind))throw new Error('Invalid TMS document kind');
   if(kind===this.kind&&!this.entries().some(r=>r.changed))return this.originalBytes;
   const lines=['TMSET7'];for(const r of this.#records.values())lines.push(`${r.key}=${r.type}:${r.key==='document.kind'?kind:r.raw}`);lines.push('END','');
   const bytes=utf8.encode(lines.join('\n'));if(bytes.length>TMS_LIMIT.documentBytes)throw new Error('TMS document exceeds firmware budget');return bytes;
 }
}
