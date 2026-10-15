// Explicit hardware acceptance only. The browser API shim transports the real
// page's frames unchanged; it does not simulate the firmware or its signatures.
import {spawn} from 'node:child_process';import {createInterface} from 'node:readline';import {fileURLToPath} from 'node:url';import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {FrameDecoder,FRAME} from '../../site/device/src/hostlink.js';import {decodeManagement} from '../../site/device/src/management-protocol.js';import {decode,verifySigned,equal} from '../../site/geocaching/src/protocol.js';import {validateAuthorInfo} from '../../site/device/src/author-identity.js';
import {decodeDeviceDraft,encodeDraftEdit} from '../../site/geocaching/src/device-draft.js';
const allowed=new Set([0,1,2,3,32,33,34,35,36,37,38,64,65,66,71,72,73,74,75,76,77]),hex=value=>Buffer.from(value).toString('hex');
export class GeocachingHardwareExchange{
 constructor({port,python,fingerprint,namePrefix,resumeDraftId=null,resumeBaseHash=null,resumeSignature=null,calls=[],signedVersions=[],checkpointPath=null}){
  if(!/^COM\d+$/.test(port)||!/^[a-f0-9]{64}$/.test(fingerprint)||!namePrefix)throw Error('Explicit port, expected author and isolated test-name prefix required');
  this.fingerprint=fingerprint;this.namePrefix=namePrefix;this.calls=calls;this.signedVersions=signedVersions;this.signatures=new Set();this.confirmations=new Set();this.confirmed=new Set();this.drafts=new Set();this.transfers=new Map();this.outputs=new Map();this.completed=new Set();this.pending=null;this.queue=Promise.resolve();this.broken=null;
  if(resumeDraftId!==null&&(!/^[a-f0-9]{32}$/.test(resumeDraftId)||!/^Wio网页验收-[a-z0-9]+$/.test(namePrefix)))throw Error('Resume requires an explicit isolated acceptance ID and name');this.resumeDraftId=resumeDraftId;
  if(resumeBaseHash!==null)assert.ok(resumeDraftId&&resumeBaseHash instanceof Uint8Array&&resumeBaseHash.length===32,'Confirmed resume requires an exact signed base');
  this.resumeBaseHash=resumeBaseHash?.slice()??null;
  this.resumeSignature=resumeSignature;
  if(resumeSignature){
   const row=resumeSignature,record=decode(row.raw,4096);
   assert.ok(resumeBaseHash&&hex(record[2])===resumeDraftId&&record[3]===2&&record[8]===namePrefix+'-v2'&&equal(record[4],resumeBaseHash));
   assert.ok(row.transferId.length===16&&row.hash.length===32&&BigInt(row.draftGeneration)>1n);
   this.transfers.set(hex(row.transferId),{kind:5,total:row.raw.length,hash:row.hash.slice(),content:row.raw.slice(),offset:row.raw.length,sealed:true});
  }
  this.checkpointPath=checkpointPath;
  this.child=spawn(python,[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),port],{windowsHide:true,stdio:['pipe','pipe','pipe']});this.stderr='';this.child.stderr.on('data',bytes=>{this.stderr=(this.stderr+bytes).slice(-4096);});
  this.done=new Promise((resolve,reject)=>{this.child.on('error',error=>{this.broken=error;this.pending?.reject(error);this.pending=null;reject(error);});this.child.on('exit',code=>{if(this.pending){this.pending.reject(Error('Hardware serial process exited: '+code));this.pending=null;}resolve(code);});});this.done.catch(()=>{});
  this.lines=createInterface({input:this.child.stdout});this.lines.on('line',line=>{try{const reply=JSON.parse(line),pending=this.pending;this.pending=null;if(pending)reply.error?pending.reject(Error(reply.error)):pending.resolve(reply);}catch(error){this.pending?.reject(error);this.pending=null;}});
 }
 get signCount(){return this.signatures.size;}
 get confirmCount(){return this.confirmed.size;}
 exchange(raw){const operation=this.queue.then(()=>this.run(raw));this.queue=operation.catch(error=>{this.broken=error;});return operation;}
 async run(raw){
  if(this.broken)throw this.broken;const bytes=Uint8Array.from(raw),frames=new FrameDecoder().push(bytes);assert.equal(frames.length,1);const frame=frames[0];let request;
  if(frame.type===FRAME.MANAGEMENT_REQUEST){request=decodeManagement(frame.payload);assert.ok(allowed.has(request.code),'Hardware test rejected unrelated operation '+request.code);this.guard(request);this.calls.push(request.code);}else assert.equal(frame.type,FRAME.HELLO);
  const response=frame.type===FRAME.HELLO?FRAME.HELLO_ACK:FRAME.MANAGEMENT_RESPONSE;
  if(request?.code===71&&this.checkpointPath){
   const body=request.body,row=this.transfers.get(hex(body[0]));
   // Retain public input and exact correlation BEFORE admission. An abandoned
   // test browser must not lose a potentially reserved signed revision.
   await fs.writeFile(this.checkpointPath,JSON.stringify({version:1,authorFingerprint:this.fingerprint,authorGeneration:this.author.generation,
    testName:this.namePrefix,draftId:hex(body[5]),draftGeneration:String(body[6]),transferId:hex(body[0]),hash:hex(body[1]),bytes:body[2],
    operationId:hex(body[4]),owner:hex(request.session),unsignedInput:Buffer.from(row.content).toString('base64')},null,2));
  }
  if(request?.code===75&&this.checkpointPath){
   const body=request.body,row=this.transfers.get(hex(body[0])),signed=decode(row.content,4166),record=decode(signed[0],4096);
   // Preserve the original public proof before confirmation admission too.
   // Neither the browser's lifetime nor a lost reply may discard recovery data.
   await fs.writeFile(path.join(path.dirname(this.checkpointPath),'confirmation-recovery.json'),JSON.stringify({version:1,
    authorFingerprint:this.fingerprint,authorGeneration:this.author.generation,testName:this.namePrefix,draftId:hex(record[2]),
    revision:record[3],transferId:hex(body[0]),hash:hex(body[1]),bytes:row.total,owner:hex(request.session),
    signedInput:Buffer.from(row.content).toString('base64'),receipt:{raw:[...body[3]],signature:[...body[4]],publicKey:[...body[5]],epoch:[...body[6]],delivery:[...decode(body[3],192)[1]]}},null,2));
  }
  const reply=await new Promise((resolve,reject)=>{this.pending={resolve,reject};this.child.stdin.write(JSON.stringify({hex:hex(bytes),sequence:frame.sequence,response})+'\n',error=>{if(error){this.pending=null;reject(error);}});});
  const result=Uint8Array.from(Buffer.from(reply.hex,'hex')),responseFrames=new FrameDecoder().push(result);assert.equal(responseFrames.length,1);assert.equal(responseFrames[0].type,response);assert.equal(responseFrames[0].sequence,frame.sequence);
  if(request){const value=decodeManagement(responseFrames[0].payload);assert.ok(equal(value.session,request.session)&&equal(value.request,request.request));this.responses??=[];this.responses.push({operation:request.code,status:value.code});if(value.code===0)await this.observe(request,value.body);}
  return [...result];
 }
 guard({code,body}){
  if(![0,1,2,3,64].includes(code))assert.ok(this.author?.fingerprint===this.fingerprint&&this.author,'Device author must match the explicit baseline before maintenance writes');
  if(code===32){assert.ok([2,5,6].includes(body[1]));assert.ok(body[2]>0&&body[2]<=4166);const id=hex(body[0]),old=this.transfers.get(id);if(old){assert.equal(old.kind,body[1]);assert.equal(old.total,body[2]);assert.ok(equal(old.hash,body[3]));}else this.transfers.set(id,{kind:body[1],total:body[2],hash:body[3].slice(),content:new Uint8Array(body[2]),offset:0,sealed:false});}
  if([33,34,35,36,37].includes(code)){assert.ok(this.transfers.has(hex(body[0])),'Unrelated transfer rejected');if(code===33){const row=this.transfers.get(hex(body[0]));assert.ok(body[1]>=0&&body[1]+body[2].length<=row.total);}}
  if(code===38)assert.ok(Array.isArray(body)&&body.length===2&&[5,6].includes(body[0])&&body[1]===null,'Unrelated staging query rejected');
  if(code===66){const row=this.transfers.get(hex(body[0]));assert.ok(row?.sealed&&row.kind===2);const edit=decode(row.content,4166);assert.ok(edit[0]===null&&edit[1]===null&&edit[2][3].startsWith(this.namePrefix));assert.ok([0,2].includes(edit[2][0]));assert.equal(edit[2][1],310000000);assert.equal(edit[2][2],1210000000);assert.ok(BigInt(body[2])===0n||this.drafts.has(hex(body[1])),'Existing unrelated draft rejected');}
  if(code===65)assert.ok(this.drafts.has(hex(body[0])),'Unrelated draft detail rejected');
  if(code===71){const row=this.transfers.get(hex(body[0]));assert.ok(row?.sealed&&row.kind===5);const record=decode(row.content,4166);assert.ok(record[8].startsWith(this.namePrefix));assert.ok(this.drafts.has(hex(body[5])));}
  if([72,73,74].includes(code))assert.ok(this.signatures.has(hex(body[0])),'Unrelated signing operation rejected');
  if(code===75){const row=this.transfers.get(hex(body[0]));assert.ok(row?.sealed&&row.kind===6);const signed=decode(row.content,4166),record=decode(signed[0],4096);assert.ok(this.drafts.has(hex(record[2]))&&record[8].startsWith(this.namePrefix));}
  if([76,77].includes(code))assert.ok(this.confirmations.has(hex(body[0])),'Unrelated confirmation rejected');
 }
 async observe({code,body},value){
  if(code===1||code===2){const author=await validateAuthorInfo(value.slice(0,4));assert.equal(author.fingerprint,this.fingerprint,'Physical device author differs from acceptance baseline');this.author=author;}
  if(code===65){
   assert.ok(equal(value[0],body[0])&&Number.isInteger(value[2])&&value[2]>0&&value[2]<=32768);
   const key='draft:'+hex(body[0]);let output=this.outputs.get(key);
   if(!output){assert.equal(value[3],0);output={bytes:new Uint8Array(value[2]),generation:value[1],offset:0};this.outputs.set(key,output);}
   assert.equal(value[2],output.bytes.length);assert.equal(BigInt(value[1]),BigInt(output.generation));assert.equal(value[3],output.offset);
   output.bytes.set(value[4],output.offset);output.offset+=value[4].length;
   if(output.offset===output.bytes.length){
    const {id,authorKey,baseHash,generation,...fields}=decodeDeviceDraft(body[0],output.bytes);
    assert.ok((authorKey===null||equal(authorKey,this.author.publicKey))&&fields.name.startsWith(this.namePrefix));
    assert.equal(fields.latitudeE7,310000000);assert.equal(fields.longitudeE7,1210000000);
    this.retainedInputs??=new Map();
    // Reconstruct only the two planned fixture edits from the real readback.
    // A sealed resume sends no chunks; never substitute zero-filled content or
    // bypass the existing save guard just because the device reports sealed.
    for(const edit of [{...fields,name:this.namePrefix+'-v2'},{...fields,state:2}]){
     const source=encodeDraftEdit(id,edit,generation),hash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
     this.retainedInputs.set(hex(hash),source);
    }
    this.outputs.delete(key);
   }
  }
  if(code===32&&body[1]===2&&value[3]===1){
   const row=this.transfers.get(hex(body[0])),source=this.retainedInputs?.get(hex(row.hash));
   assert.ok(equal(value[0],body[0])&&value[2]===row.total&&source?.length===row.total,'Sealed upload is not an exact planned fixture edit');
   row.content.set(source);row.offset=row.total;row.sealed=true;
  }
  if(code===33){const row=this.transfers.get(hex(body[0]));row.content.set(body[2],body[1]);row.offset=Math.max(row.offset,body[1]+body[2].length);}
  if(code===35){const row=this.transfers.get(hex(body[0]));assert.equal(row.offset,row.total);assert.ok(equal(new Uint8Array(await crypto.subtle.digest('SHA-256',row.content)),row.hash));row.sealed=true;}
  if(code===66)this.drafts.add(hex(body[1]));
  if(code===64&&value[3]&&hex(value[3][0])===this.resumeDraftId&&!this.drafts.has(this.resumeDraftId)){const row=value[3];assert.equal(row[3],this.namePrefix+(this.resumeSignature?'-v2':''));assert.equal(row[5],310000000);assert.equal(row[6],1210000000);assert.equal(row[4],0);assert.equal(row[8][0],this.resumeBaseHash?1:0);if(this.resumeBaseHash){assert.equal(row[8][1],1);assert.equal(row[8][2]&7,this.resumeSignature?4:0);assert.ok(equal(row[7],this.resumeBaseHash));}if(this.resumeSignature)assert.equal(BigInt(row[1]),BigInt(this.resumeSignature.draftGeneration));assert.ok(row[2]===null||equal(row[2],this.author.publicKey));this.drafts.add(this.resumeDraftId);}
  if(code===71)this.signatures.add(hex(body[4]));
  if(code===75)this.confirmations.add(hex(body[0]));
  if([75,76].includes(code)&&value[3]===1&&value[7]===1)this.confirmed.add(hex(body[0]));
  if(code===73){const id=hex(body[0]);if(this.completed.has(id))return;let output=this.outputs.get(id);if(!output){assert.equal(value[1],0);assert.ok(value[2]>0&&value[2]<=4166);output={bytes:new Uint8Array(value[2]),hash:value[3].slice(),offset:0};this.outputs.set(id,output);}assert.equal(value[2],output.bytes.length);assert.ok(equal(value[3],output.hash));if(value[1]<output.offset){assert.ok(value[1]>=0&&value[1]+value[4].length<=output.offset);assert.ok(equal(output.bytes.slice(value[1],value[1]+value[4].length),value[4]),'Retried signature bytes changed');return;}assert.equal(value[1],output.offset);output.bytes.set(value[4],output.offset);output.offset+=value[4].length;if(output.offset===output.bytes.length){const signed=decode(output.bytes,4166),verified=await verifySigned(signed);assert.ok(equal(verified.record[1],this.author.publicKey));this.signedVersions.push(signed);this.completed.add(id);this.outputs.delete(id);}}
 }
 async close(){await this.queue;this.child.stdin.end();const code=await this.done;this.lines.close();if(code!==0&&!this.broken)throw Error('Hardware serial process exited '+code+': '+this.stderr);}
}
