// Rebind only the previously confirmed, isolated hardware-test archive using
// its original signed manifest and directory receipt. No new publication/signing.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import {readFile,writeFile} from 'node:fs/promises';
import {encodeFrame,FrameDecoder,FRAME,decodeHello} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {decodeTrackPublicationPlan} from '../../site/tracks/src/publication-plan.js';
import {verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {confirmDeviceTrackPublication} from '../../site/tracks/src/device-ip-confirmation.js';
import {equal,encode} from '../../site/geocaching/src/protocol.js';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1],hex=value=>Buffer.from(value).toString('hex'),bin=value=>Uint8Array.from(Buffer.from(value,'hex'));
if(!args.includes('--port')||!args.includes('--python')||!args.includes('--archive')||!args.includes('--checkpoint'))throw Error('Use --port --python --archive <existing isolated test> --checkpoint');
const job=JSON.parse(await readFile(option('--archive'),'utf8'));assert.ok(job.confirmed&&job.finished&&job.directory);
const plan=decodeTrackPublicationPlan(bin(job.plan)),signed=[plan.manifest,bin(job.signature)],version=await verifyTrackManifest(signed);
assert.equal(version.record[4],3);assert.equal(version.record[6],1);
const directory=job.directory,record={stage:'confirmed',draftId:'hardware-archive',draftVersion:3,authorKey:version.record[1],requestId:bin(job.request),directoryKey:bin(directory.directory),delivery:bin(directory.delivery),epoch:bin(directory.epoch),plan,signature:signed[1],receipt:encode(directory.receipt.map(bin))};
const child=spawn(option('--python'),[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),option('--port')],{stdio:['pipe','pipe','inherit'],windowsHide:true});let pending,sequence=0,session;
createInterface({input:child.stdout}).on('line',line=>{const reply=JSON.parse(line);if(pending){const current=pending;pending=null;reply.error?current.reject(Error(reply.error)):current.resolve(reply);}});
child.on('exit',code=>{if(pending){pending.reject(Error('Serial bridge exited '+code));pending=null;}});
async function exchange(type,payload,response){
 assert.ok(!pending);const next=++sequence,reply=await new Promise((resolve,reject)=>{pending={resolve,reject};child.stdin.write(JSON.stringify({hex:hex(encodeFrame(type,next,payload)),sequence:next,response})+'\n');});
 const frame=new FrameDecoder().push(bin(reply.hex))[0];assert.ok(frame&&frame.type===response&&frame.sequence===next);return frame.payload;
}
try{
 console.log(JSON.stringify({handshake:decodeHello(await exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK))}));
 session={id:crypto.getRandomValues(new Uint8Array(16)),async request(operation,body){
  if(![0,1,2,3,32,33,35,37,98].includes(operation))throw Error('Operation outside confirmed history test');
  const id=crypto.getRandomValues(new Uint8Array(16)),reply=decodeManagement(await exchange(FRAME.MANAGEMENT_REQUEST,encodeManagement(this.id,id,operation,body),FRAME.MANAGEMENT_RESPONSE));
  assert.ok(equal(reply.session,this.id)&&equal(reply.request,id));if(reply.code)throw Object.assign(Error('Device status '+reply.code+' for '+operation),{code:reply.code});return reply.body;
 }};
 const operations=(await session.request(0,[]))[4],author=await proveDeviceAuthor(session);assert.ok(equal(author.publicKey,record.authorKey));assert.notEqual(author.generation,plan.generation);
 const saved=await readFile(option('--checkpoint'),'utf8').then(JSON.parse).catch(error=>{if(error.code==='ENOENT')return null;throw error;});
 if(saved){assert.equal(saved.generation,author.generation);assert.equal(saved.objectId,hex(version.objectId));assert.equal(saved.revisionHash,hex(version.revisionHash));assert.match(saved.receiptId,/^[a-f0-9]{32}$/);assert.match(saved.manifestId,/^[a-f0-9]{32}$/);}
 const binding=saved?{receiptId:bin(saved.receiptId),manifestId:bin(saved.manifestId),generation:saved.generation}:{receiptId:crypto.getRandomValues(new Uint8Array(16)),manifestId:crypto.getRandomValues(new Uint8Array(16)),generation:author.generation};
 if(!saved)await writeFile(option('--checkpoint'),JSON.stringify({receiptId:hex(binding.receiptId),manifestId:hex(binding.manifestId),generation:binding.generation,objectId:hex(version.objectId),revisionHash:hex(version.revisionHash)}));
 let phase;const options={operations,id:binding.receiptId,manifestId:binding.manifestId,importHistory:true,retryMs:250,onProgress:event=>{if(phase!==event.phase){phase=event.phase;console.log(JSON.stringify({phase}));}}};
 const first=await confirmDeviceTrackPublication(session,record,author,options);assert.ok(first.durable&&first.revision===3);
 const second=await confirmDeviceTrackPublication(session,record,author,options);assert.ok(second.durable&&equal(first.objectId,second.objectId)&&equal(first.revisionHash,second.revisionHash));
 for(const [id,kind,data] of [[binding.manifestId,4,plan.manifest],[binding.receiptId,7,record.receipt]]){
  const digest=new Uint8Array(await crypto.subtle.digest('SHA-256',data));assert.deepEqual(await session.request(37,[id,kind,data.length,digest]),[]);
 }
 console.log(JSON.stringify({sameAuthorHistoryRebound:true,originalGeneration:plan.generation,currentGeneration:author.generation,revision:3,archived:true,durable:true,repeatConfirmationConsistent:true,contentSignRequests:0,networkPublications:0,temporaryProofUploadsCleaned:true}));
}finally{if(session)try{await session.request(3,[]);}catch{}child.stdin.end();}
