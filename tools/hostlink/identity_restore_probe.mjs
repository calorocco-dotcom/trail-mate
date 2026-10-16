// Explicit same-account Reticulum restore, or read-only post-reboot verification.
// No private backup/password/configuration bytes are retained in files or logs.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import {readFile,writeFile} from 'node:fs/promises';
import {encodeFrame,FrameDecoder,FRAME,decodeHello} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {exportEncryptedDeviceIdentity} from '../../site/device/src/identity-backup-export.js';
import {previewEncryptedDeviceIdentity} from '../../site/device/src/identity-backup-preview.js';
import {verifyRestoredDeviceIdentity} from '../../site/device/src/identity-restore-verification.js';
import {readDeviceRadioIdentity} from '../../site/device/src/radio-identity.js';
import {readConfigSnapshot} from '../../site/device/src/config-snapshot.js';
import {equal} from '../../site/geocaching/src/protocol.js';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1],hex=value=>Buffer.from(value).toString('hex'),restore=args.includes('--restore');
if(!args.includes('--port')||!args.includes('--python')||!args.includes('--checkpoint')||restore===args.includes('--verify'))throw Error('Use --port --python --checkpoint and exactly one of --restore / --verify');
const child=spawn(option('--python'),[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),option('--port')],{stdio:['pipe','pipe','inherit'],windowsHide:true});let pending,sequence=0;
createInterface({input:child.stdout}).on('line',line=>{const reply=JSON.parse(line);if(pending){const current=pending;pending=null;reply.error?current.reject(Error(reply.error)):current.resolve(reply);}});
child.on('exit',code=>{if(pending){pending.reject(Error('Serial bridge exited '+code));pending=null;}});
async function exchange(type,payload,response){
 assert.ok(!pending);const next=++sequence,raw=encodeFrame(type,next,payload);
 try{const reply=await new Promise((resolve,reject)=>{pending={resolve,reject};child.stdin.write(JSON.stringify({hex:hex(raw),sequence:next,response})+'\n');});
  const frame=new FrameDecoder().push(Uint8Array.from(Buffer.from(reply.hex,'hex')))[0];assert.ok(frame&&frame.type===response&&frame.sequence===next);return frame.payload;
 }finally{raw.fill(0);}
}
try{
 console.log(JSON.stringify({handshake:decodeHello(await exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK))}));
 const session={id:crypto.getRandomValues(new Uint8Array(16)),async request(operation,body){
  if(![0,1,2,3,4,6,7,17].includes(operation)&&!(operation===5&&restore))throw Error('Probe operation not admitted');
  const id=crypto.getRandomValues(new Uint8Array(16));let raw,incoming;
  try{raw=encodeManagement(this.id,id,operation,body);incoming=await exchange(FRAME.MANAGEMENT_REQUEST,raw,FRAME.MANAGEMENT_RESPONSE);const reply=decodeManagement(incoming);
   assert.ok(equal(reply.session,this.id)&&equal(reply.request,id));if(reply.code)throw Object.assign(Error('Device status '+reply.code+' for '+operation),{code:reply.code});return reply.body;
  }finally{raw?.fill(0);incoming?.fill(0);}
 }};
 const operations=(await session.request(0,[]))[4];assert.ok([1,2,4,5,7,17].every(op=>operations.includes(op)));
 const author=await proveDeviceAuthor(session),radios=[];
 for(const kind of [1,2]){const info=await readDeviceRadioIdentity(session,kind,author,{operations});radios.push({kind,present:info.present,generation:info.generation,publicKey:info.publicKey?hex(info.publicKey):null});}
 const snapshot=await readConfigSnapshot(session),configHash=hex(new Uint8Array(await crypto.subtle.digest('SHA-256',snapshot.document.originalBytes)));
 if(restore){
  const password=hex(crypto.getRandomValues(new Uint8Array(32)));let exported,preview;
  try{exported=await exportEncryptedDeviceIdentity(session,password,{consent:true});preview=await previewEncryptedDeviceIdentity(exported.bytes,password,{session,operations});
   assert.equal(preview.metadata.changesIdentity,false);assert.equal(preview.metadata.targetGeneration,author.generation);assert.equal(preview.metadata.publicKey,hex(author.publicKey));
   const expectation={publicKey:preview.metadata.publicKey,identityHash:preview.metadata.identityHash,delivery:preview.metadata.delivery,generation:author.generation+1};
   await writeFile(option('--checkpoint'),JSON.stringify({expectation,radios,configHash,fields:snapshot.document.size}));
   try{const result=await preview.restore({consent:true});console.log(JSON.stringify({sameReticulumIdentityRestore:result.state,expectedGeneration:result.expectation.generation,publicKeyUnchanged:true,retainedSecretFiles:0}));}
   catch(error){if(error.code!=='IDENTITY_RESTORE_OUTCOME_UNKNOWN')throw error;console.log(JSON.stringify({sameReticulumIdentityRestore:'indeterminate',expectedGeneration:error.expectation.generation,retainedSecretFiles:0}));}
  }finally{preview?.close();exported?.bytes.fill(0);}
 }else{
  const checkpoint=JSON.parse(await readFile(option('--checkpoint'),'utf8'));await verifyRestoredDeviceIdentity(session,checkpoint.expectation);
  assert.deepEqual(radios,checkpoint.radios);assert.equal(configHash,checkpoint.configHash);assert.equal(snapshot.document.size,checkpoint.fields);
  console.log(JSON.stringify({ReticulumRestoreVerified:true,generation:author.generation,publicKeyUnchanged:true,bothRFIdentitiesUnchanged:true,fullConfigBytesUnchanged:true,fields:snapshot.document.size}));await session.request(3,[]);
 }
 snapshot.document.originalBytes.fill(0);
}finally{child.stdin.end();}
