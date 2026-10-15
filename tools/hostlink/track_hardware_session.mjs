// Opt-in final acceptance only. Reuse production signing/confirmation clients;
// never simulate the physical author's proof, signature or durable response.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import fs from 'node:fs/promises';
import {FrameDecoder,FRAME,encodeFrame,decodeHello} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {decode,equal,encode} from '../../site/geocaching/src/protocol.js';
import {decodeTrackPublicationPlan} from '../../site/tracks/src/publication-plan.js';
import {signDeviceTrackManifest} from '../../site/tracks/src/device-local-signature.js';
import {confirmDeviceTrackPublication} from '../../site/tracks/src/device-ip-confirmation.js';
import {encodeTrackIPCheckpoint} from '../../site/tracks/src/ip-publication-journal.js';

const allowed=new Set([0,1,2,3,32,33,34,35,37,38,96,97,99]);
const hex=bytes=>Buffer.from(bytes).toString('hex');
const randomId=()=>crypto.getRandomValues(new Uint8Array(16));

export class TrackHardwareSession {
 constructor({port,python,fingerprint,reservation,checkpointPath}) {
  assert.match(port,/^COM\d+$/);assert.match(fingerprint,/^[a-f0-9]{64}$/);
  assert.ok(reservation?.reservedForFinalAcceptance&&reservation.finalCandidateReady,
            'The reserved device may be opened only for final candidate acceptance');
  this.fingerprint=fingerprint;this.checkpointPath=checkpointPath;
  this.id=randomId();this.sequence=0;this.pending=null;this.uploads=new Map();
  this.child=spawn(python,[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),port],
                   {windowsHide:true,stdio:['pipe','pipe','pipe']});
  this.stderr='';this.child.stderr.on('data',data=>{this.stderr=(this.stderr+data).slice(-4096);});
  this.done=new Promise((resolve,reject)=>{
   this.child.on('error',error=>{this.pending?.reject(error);this.pending=null;reject(error);});
   this.child.on('exit',code=>{this.pending?.reject(Error('Serial process exited: '+code));this.pending=null;resolve(code);});
  });this.done.catch(()=>{});
  this.lines=createInterface({input:this.child.stdout});this.lines.on('line',line=>{
   const active=this.pending;this.pending=null;if(!active)return;
   try{const reply=JSON.parse(line);reply.error?active.reject(Error(reply.error)):active.resolve(reply);}
   catch(error){active.reject(error);}
  });
 }
 async exchange(type,payload,response) {
  assert.equal(this.pending,null,'Hardware requests must be sequential');
  const sequence=++this.sequence;
  const reply=await new Promise((resolve,reject)=>{
   this.pending={resolve,reject};this.child.stdin.write(JSON.stringify({hex:hex(encodeFrame(type,sequence,payload)),sequence,response})+'\n',
    error=>{if(error){this.pending=null;reject(error);}});
  });
  const frames=new FrameDecoder().push(Uint8Array.from(Buffer.from(reply.hex,'hex')));
  assert.equal(frames.length,1);assert.equal(frames[0].type,response);assert.equal(frames[0].sequence,sequence);
  return frames[0].payload;
 }
 async request(operation,body) {
  assert.ok(allowed.has(operation),'Unrelated device operation rejected');
  if(![0,1,2].includes(operation))assert.ok(this.author?.verified&&this.author.fingerprint===this.fingerprint);
  if(operation===32){
   assert.ok([3,4,7,8,9,10].includes(body[1])&&body[2]>0&&body[2]<=8192);
   this.uploads.set(hex(body[0]),{id:body[0].slice(),kind:body[1],total:body[2],hash:body[3].slice()});
  }
  if([33,34,35,37].includes(operation))assert.ok(this.uploads.has(hex(body[0])),'Unrelated upload rejected');
  const id=randomId(),response=decodeManagement(await this.exchange(FRAME.MANAGEMENT_REQUEST,
    encodeManagement(this.id,id,operation,body),FRAME.MANAGEMENT_RESPONSE));
  assert.ok(equal(response.session,this.id)&&equal(response.request,id));
  if(response.code!==0)throw Object.assign(Error('Device status '+response.code+' (operation '+operation+(operation===32?', kind '+body[1]:'')+')'),
    {code:response.code,operation,uploadKind:operation===32?body[1]:undefined});
  return response.body;
 }
 async open() {
  const hello=decodeHello(await this.exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK));
  // The current service hardcodes its diagnostic firmware label to "dev".
  // The verified upload receipt binds the candidate; validate its wire contract.
  assert.equal(hello.version,1,'Final acceptance requires HostLink protocol v1');
  const caps=await this.request(0,[]);this.operations=caps[4];
  assert.ok([1,2,32,33,35,37,38,96,97,99].every(operation=>this.operations.includes(operation)));
  this.author=await proveDeviceAuthor(this);assert.equal(this.author.fingerprint,this.fingerprint);
  return this.author;
 }
 async attachmentBindings(manifest) {
  assert.ok(this.author?.verified&&this.author.fingerprint===this.fingerprint&&this.checkpointPath);
  const manifestHash=hex(new Uint8Array(await crypto.subtle.digest('SHA-256',manifest)));
  const file=this.checkpointPath+'.attachments.json';let journal;
  try{journal=JSON.parse(await fs.readFile(file,'utf8'));}catch(error){if(error.code!=='ENOENT')throw error;}
  if(journal){
   assert.ok(journal.version===1&&journal.author===this.fingerprint&&journal.generation===this.author.generation,
     'Retained physical attachment inputs belong to a different identity');
   assert.ok(Array.isArray(journal.entries)&&journal.entries.length<=3);
   const hashes=new Set();
   for(const row of journal.entries){
    assert.match(row.manifestHash,/^[a-f0-9]{64}$/);assert.ok(!hashes.has(row.manifestHash));hashes.add(row.manifestHash);
    for(const value of [row.index,row.package,row.photo])assert.match(value,/^[a-f0-9]{32}$/);
    assert.equal(new Set([row.index,row.package,row.photo]).size,3);
   }
  }else journal={version:1,author:this.fingerprint,generation:this.author.generation,entries:[]};
  let binding=journal.entries.find(row=>row.manifestHash===manifestHash);
  if(!binding){
   assert.ok(journal.entries.length<3,'Physical lifecycle only admits its three retained revisions');
   binding={manifestHash,index:hex(randomId()),package:hex(randomId()),photo:hex(randomId())};
   journal.entries.push(binding);
   // Retain the same random IDs before any device upload, as the actual page
   // does in its draft store. Failed/reopened tests must not leak new slots.
   await fs.writeFile(file,JSON.stringify(journal,null,2));
  }
  const id=value=>Uint8Array.from(Buffer.from(value,'hex'));
  return {indexUploadId:id(binding.index),packageUploadId:id(binding.package),attachmentUploadIds:[{id:'photo',uploadId:id(binding.photo)}]};
 }
 async sign(raw) {
  const [encodedPlan,kml,index,photo]=decode(Uint8Array.from(raw),16384),plan=decodeTrackPublicationPlan(encodedPlan);
  const manifest=decode(plan.manifest,4096);
  assert.ok(['Network trail','Updated network trail'].includes(manifest[9]),'Only the isolated test trail may be signed');
  assert.ok(equal(manifest[1],this.author.publicKey));
  const bindings=await this.attachmentBindings(plan.manifest);
  await this.cleanupExactStaging(4,plan.manifest.length,new Uint8Array(await crypto.subtle.digest('SHA-256',plan.manifest)));
  const result=await signDeviceTrackManifest(this,{...plan,kml,author:this.author,
    attachmentIndex:index,attachmentFiles:[{id:'photo',content:photo}],
    ...bindings},
    {operations:this.operations});
  this.last={plan,signed:result.signed};return [...encode(result.signed)];
 }
 async confirm(raw) {
  const [encodedPlan,signature,receipt,directoryKey,delivery,epoch,requestId]=decode(Uint8Array.from(raw),8192);
  const plan=decodeTrackPublicationPlan(encodedPlan);
  assert.ok(equal(plan.manifest,this.last?.signed[0])&&equal(signature,this.last?.signed[1]));
  const record={stage:'confirmed',draftId:'hardware-network-trail',draftVersion:decode(plan.manifest,4096)[4],
    authorKey:this.author.publicKey,requestId,directoryKey,delivery,epoch,plan,signature,receipt};
  // Preserve exact public input and receipt before the durable device operation.
  await fs.writeFile(this.checkpointPath,JSON.stringify({version:1,author:this.fingerprint,
    checkpoint:Buffer.from(encodeTrackIPCheckpoint(record)).toString('base64')},null,2));
  const result=await confirmDeviceTrackPublication(this,record,this.author,{id:randomId(),operations:this.operations});
  assert.ok(result.confirmed&&result.durable);
  // A resumed signature was created in an earlier USB session. Its exact KML
  // and manifest uploads are still known from the retained, confirmed plan.
  this.uploads.set(hex(plan.kmlUploadId),{id:plan.kmlUploadId,kind:3,total:plan.fileBytes,hash:plan.fileHash});
  this.uploads.set(hex(plan.manifestUploadId),{id:plan.manifestUploadId,kind:4,total:plan.manifest.length,
    hash:new Uint8Array(await crypto.subtle.digest('SHA-256',plan.manifest))});
  for(const row of this.uploads.values())await this.request(37,[row.id,row.kind,row.total,row.hash]);
  await this.cleanupExactStaging(3,plan.fileBytes,plan.fileHash);
  await this.cleanupConfirmedIndex(plan);
  this.uploads.clear();return {confirmed:true,durable:true,revision:result.revision};
 }
 async cleanupConfirmedIndex(plan) {
  const assets=decode(plan.manifest,4096)[15];if(!assets)return;
  await this.cleanupExactStaging(8,assets[1],assets[0]);
 }
 async cleanupExactStaging(kind,total,hash) {
  const matches=new Map();let cursor=null,ended=false;
  for(let page=0;page<32&&!ended;page++){
   const reply=await this.request(38,[kind,cursor]);assert.ok(Array.isArray(reply)&&reply.length===3);
   const [media,next,row]=reply;ended=next===null;cursor=ended?null:[media,next];
   if(row&&row[1]===kind&&row[2]===total&&equal(row[3],hash)&&row[4]===this.author.generation)
    matches.set(hex(row[0]),row.slice(0,4));
  }
  assert.ok(ended,'Temporary index inventory did not finish');
  for(const binding of matches.values()){
   this.uploads.set(hex(binding[0]),{id:binding[0],kind:binding[1],total:binding[2],hash:binding[3]});
   await this.request(37,binding);
  }
 }
 async close() {
  this.child.stdin.end();const code=await this.done;this.lines.close();
  if(code!==0)throw Error('Serial transport failed: '+code+' '+this.stderr);
 }
}
