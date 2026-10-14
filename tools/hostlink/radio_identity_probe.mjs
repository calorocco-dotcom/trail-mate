// Read/export by default; explicit --restore-kind restores exactly the exported
// current RF key and advances its generation. --verify only reads after reboot.
// Secret responses and encryption passwords stay transient; logs contain public results.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import {readFile,writeFile} from 'node:fs/promises';
import {encodeFrame,FrameDecoder,FRAME,decodeHello} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {readDeviceRadioIdentity,exportEncryptedRadioIdentity,decryptRadioIdentityBackup,clearRadioIdentityBackup,restoreRadioIdentityBackup,verifyRestoredRadioIdentity} from '../../site/device/src/radio-identity.js';
import {exportEncryptedDeviceIdentity} from '../../site/device/src/identity-backup-export.js';
import {equal} from '../../site/geocaching/src/protocol.js';

const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
if(!args.includes('--port')||!args.includes('--python'))throw Error('Use --port COMxx --python <Python with pyserial>');
const restoreKind=args.includes('--restore-kind')?Number(option('--restore-kind')):null;
if(restoreKind!==null&&(![1,2].includes(restoreKind)||!args.includes('--checkpoint')||args.includes('--verify')))throw Error('Explicit kind 1/2 and public --checkpoint required');
const child=spawn(option('--python'),[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),option('--port')],{stdio:['pipe','pipe','inherit'],windowsHide:true});
let pending,sequence=0;
createInterface({input:child.stdout}).on('line',line=>{const reply=JSON.parse(line);if(pending){const active=pending;pending=null;reply.error?active.reject(Error(reply.error)):active.resolve(reply);}});
child.on('exit',code=>{if(pending){pending.reject(Error('Serial transport exited '+code));pending=null;}});
async function exchange(type,payload,response){
 if(pending)throw Error('Sequential exchange required');const next=++sequence,raw=encodeFrame(type,next,payload);
 try{const reply=await new Promise((resolve,reject)=>{pending={resolve,reject};child.stdin.write(JSON.stringify({hex:Buffer.from(raw).toString('hex'),sequence:next,response})+'\n');});
  const incoming=new FrameDecoder().push(Uint8Array.from(Buffer.from(reply.hex,'hex')))[0];
  if(!incoming||incoming.type!==response||incoming.sequence!==next)throw Error('Unexpected serial frame');return incoming.payload;
 }finally{raw.fill(0);}
}
try{
 console.log(JSON.stringify({handshake:decodeHello(await exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK))}));
 const session={id:crypto.getRandomValues(new Uint8Array(16)),async request(operation,body,{requestId}={}){
  if(![0,1,2,4,6,7,8].includes(operation)&&!(operation===9&&restoreKind!==null))throw Error('Probe operation not admitted');
  const id=requestId??crypto.getRandomValues(new Uint8Array(16));let raw,response;
  try{raw=encodeManagement(this.id,id,operation,body);response=await exchange(FRAME.MANAGEMENT_REQUEST,raw,FRAME.MANAGEMENT_RESPONSE);const reply=decodeManagement(response);
   if(!equal(reply.session,this.id)||!equal(reply.request,id))throw Error('Management correlation mismatch');
   if(reply.code!==0)throw Object.assign(Error('Device rejected operation '+operation+' with status '+reply.code),{code:reply.code});return reply.body;
  }finally{raw?.fill(0);response?.fill(0);}
 }};
 const capabilities=await session.request(0,[]),operations=capabilities[4];
 assert.ok([1,2,7,8,9].every(operation=>operations.includes(operation)));console.log(JSON.stringify({managementOperations:operations.length,RFManagement:true}));
 if(args.includes('--verify')){
  const expectation=JSON.parse(await readFile(option('--verify'),'utf8'));
  assert.ok([1,2].includes(expectation.kind)&&/^[a-f0-9]{64}$/.test(expectation.publicKey)&&/^[a-f0-9]{128}$/.test(expectation.authorPublicKey));
  assert.ok(Number.isInteger(expectation.generation)&&expectation.generation>0&&Number.isInteger(expectation.authorGeneration)&&expectation.authorGeneration>0);
  await verifyRestoredRadioIdentity(session,expectation,{operations});console.log(JSON.stringify({RFRestoreVerifiedAfterReboot:true,kind:expectation.kind,radioGeneration:expectation.generation,accountUnchanged:true}));
 }else{
 const author=await proveDeviceAuthor(session);console.log(JSON.stringify({authorVerified:author.verified,fingerprint:author.fingerprint,generation:author.generation}));
 // Fresh random password is never written to a file or printed. No backup is retained.
 const password=Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString('hex');
 for(const kind of [1,2]){
  const info=await readDeviceRadioIdentity(session,kind,author,{operations});
  console.log(JSON.stringify({kind,present:info.present,radioGeneration:info.generation}));
  if(!info.present)continue;let exported,decoded;
  try{exported=await exportEncryptedRadioIdentity(session,kind,password,{operations,consent:true});decoded=await decryptRadioIdentityBackup(exported.bytes,password);
   assert.ok(equal(decoded.identity.publicKey,info.publicKey));assert.equal(decoded.identity.radioGeneration,info.generation);
   assert.ok(equal(decoded.identity.sourceAuthor,author.publicKey));assert.equal(decoded.identity.authorGeneration,author.generation);
   console.log(JSON.stringify({kind,signedUSBExportVerified:true,encryptionRoundtripVerified:true,encryptedBytes:exported.bytes.length}));
   if(kind===restoreKind){
    const toHex=value=>Buffer.from(value).toString('hex'),expectation={kind,publicKey:toHex(info.publicKey),generation:info.generation+1,authorPublicKey:toHex(author.publicKey),authorGeneration:author.generation};
    await writeFile(option('--checkpoint'),JSON.stringify(expectation));
    try{const result=await restoreRadioIdentityBackup(session,decoded,author,info,{operations,consent:true});console.log(JSON.stringify({sameRFKeyRestore:result.state,expectation:result.expectation}));}
    catch(error){if(error.code!=='RADIO_IDENTITY_OUTCOME_UNKNOWN')throw error;console.log(JSON.stringify({sameRFKeyRestore:'indeterminate',expectation:error.expectation}));}
    break;
   }
  }finally{clearRadioIdentityBackup(decoded);exported?.bytes.fill(0);}
 }
 if(restoreKind===null){
  if(operations.includes(4)){const result=await exportEncryptedDeviceIdentity(session,password,{operations,consent:true});console.log(JSON.stringify({ReticulumEncryptedExportVerified:true,encryptedBytes:result.bytes.length}));result.bytes.fill(0);}
  const after=await proveDeviceAuthor(session);assert.ok(equal(author.publicKey,after.publicKey));assert.equal(after.generation,author.generation);
  console.log(JSON.stringify({accountUnchanged:true,identityWrites:0,networkPublications:0,retainedSecretFiles:0}));
 }
 }
}finally{child.stdin.end();}
