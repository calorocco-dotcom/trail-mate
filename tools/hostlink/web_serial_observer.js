import {FrameDecoder,FRAME,decodeHello} from '../../site/device/src/hostlink.js';
import {decodeManagement} from '../../site/device/src/management-protocol.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
// Diagnostic page only: native SerialPort streams remain native. Do not record
// configuration bytes, private keys or field values outside this page's memory.
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const allowed=new Set([0,1,2,3,7,17]),pending=new Map(),operations=[];
let snapshots=0,opened=0,closed=0,hello,capabilities,readError,output,outputHash,configSummary;
const nativeOpen=SerialPort.prototype.open,nativeClose=SerialPort.prototype.close;
SerialPort.prototype.open=async function(...args){
 await nativeOpen.apply(this,args);opened++;
 const input=new FrameDecoder(),getReader=this.readable.getReader.bind(this.readable),getWriter=this.writable.getWriter.bind(this.writable);
 this.readable.getReader=(...args)=>{
  const reader=getReader(...args),read=reader.read.bind(reader);
  reader.read=async(...args)=>{
   const result=await read(...args);
   if(result.value)for(const frame of input.push(result.value))try{
    if(frame.type===FRAME.HELLO_ACK)hello=decodeHello(frame.payload);
    if(frame.type!==FRAME.MANAGEMENT_RESPONSE)continue;
    const reply=decodeManagement(frame.payload),op=pending.get(hex(reply.request));pending.delete(hex(reply.request));
    if(reply.code){readError='device status '+reply.code;continue;}
    if(op===0)capabilities=reply.body;if(op!==17)continue;
    const [offset,total,hash,bytes]=reply.body;
    if(offset===0){output=new Uint8Array(total);outputHash=hash.slice();}
    if(!output||offset+bytes.length>output.length)throw Error('Invalid snapshot offsets');
    output.set(bytes,offset);
    if(offset+bytes.length===output.length){
     const retained=output;output=undefined;
     try{const actual=hex(new Uint8Array(await crypto.subtle.digest('SHA-256',retained)));if(actual!==hex(outputHash))throw Error('Observed snapshot digest mismatch');configSummary={sha256:actual,fields:new TmsDocument(retained).size,bytes:retained.length};snapshots++;}finally{retained.fill(0);}
    }
   }catch(error){readError=String(error);}
   return result;
  };return reader;
 };
 this.writable.getWriter=(...args)=>{
  const writer=getWriter(...args),write=writer.write.bind(writer);
  writer.write=(bytes)=>{
   const frames=new FrameDecoder().push(bytes);if(frames.length!==1)throw Error('Hardware probe rejected non-HostLink write');const frame=frames[0];
   if(frame.type===FRAME.MANAGEMENT_REQUEST){const request=decodeManagement(frame.payload),operation=request.code;if(!allowed.has(operation))throw Error('Read-only acceptance rejected operation '+operation);operations.push(operation);pending.set(hex(request.request),operation);}
   else if(frame.type!==FRAME.HELLO)throw Error('Read-only acceptance rejected frame '+frame.type);
   return write(bytes);
  };return writer;
 };
};
SerialPort.prototype.close=async function(...args){const result=await nativeClose.apply(this,args);closed++;return result;};
globalThis.webSerialProbeSummary=()=>({nativeSerial:true,opened,closed,snapshots,hello,operations:operations.slice(),capabilityCount:capabilities?.[4]?.length,config:configSummary,readError:readError??null});
