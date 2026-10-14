// Explicit, reversible hardware test. Only public test fields and hashes are
// checkpointed; the complete configuration and secrets remain in process memory.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import {readFile,writeFile} from 'node:fs/promises';
import {encodeFrame,FrameDecoder,FRAME} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {readConfigSnapshot} from '../../site/device/src/config-snapshot.js';
import {saveConfigDocument} from '../../site/device/src/config-save.js';
import {equal} from '../../site/geocaching/src/protocol.js';

const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
if(!args.includes('--port')||!args.includes('--python')||!args.includes('--checkpoint')||
   Number(args.includes('--run'))+Number(args.includes('--verify'))!==1)
 throw Error('Use --port --python --checkpoint and exactly one of --run / --verify');
const hex=value=>Buffer.from(value).toString('hex'),bin=value=>Uint8Array.from(Buffer.from(value,'hex'));
const random=()=>crypto.getRandomValues(new Uint8Array(16)),sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const checkpoint=option('--checkpoint');
let job=await readFile(checkpoint,'utf8').then(JSON.parse).catch(error=>{if(error.code==='ENOENT')return null;throw error;}),connection;
const fields=['device.node_name','map.contour_enabled','ui.screen.timeout_ms'];
const persist=()=>writeFile(checkpoint,JSON.stringify(job));

async function configRoundtripConnect(){
 const child=spawn(option('--python'),[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),option('--port')],{stdio:['pipe','pipe','inherit'],windowsHide:true});
 let pending,sequence=0;
 const lines=createInterface({input:child.stdout});
 lines.on('line',line=>{const reply=JSON.parse(line);if(pending){const current=pending;pending=null;reply.error?current.reject(Error(reply.error)):current.resolve(reply);}});
 child.on('exit',code=>{if(pending){pending.reject(Error('Serial bridge exited '+code));pending=null;}});
 const exchange=async(type,payload,response)=>{
  assert.ok(!pending);const next=++sequence;
  const reply=await new Promise((resolve,reject)=>{pending={resolve,reject};child.stdin.write(JSON.stringify({hex:hex(encodeFrame(type,next,payload)),sequence:next,response})+'\n');});
  const frame=new FrameDecoder().push(bin(reply.hex))[0];assert.ok(frame&&frame.type===response&&frame.sequence===next);return frame.payload;
 };
 const session={id:job?bin(job.session):random(),async request(operation,body){
  if(![0,1,2,3,17,21,32,33,35].includes(operation))throw Error('Operation outside configuration roundtrip');
  if(args.includes('--verify')&&[21,32,33,35].includes(operation))throw Error('Read-only verification cannot write');
  const id=random(),reply=decodeManagement(await exchange(FRAME.MANAGEMENT_REQUEST,encodeManagement(this.id,id,operation,body),FRAME.MANAGEMENT_RESPONSE));
  assert.ok(equal(reply.session,this.id)&&equal(reply.request,id));
  if(reply.code)throw Object.assign(Error('Device status '+reply.code+' for '+operation),{code:reply.code});return reply.body;
 }};
 const close=()=>{child.stdin.end();lines.close();};
 try{await exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK);return {session,close};}catch(error){close();throw error;}
}
function configRoundtripClose(){connection?.close();connection=null;}
async function configRoundtripRead(){
 connection=await configRoundtripConnect();const author=await proveDeviceAuthor(connection.session);
 if(job){assert.equal(author.fingerprint,job.identity,'Device identity changed');assert.equal(author.generation,job.generation,'Device identity generation changed');}
 return {author,snapshot:await readConfigSnapshot(connection.session)};
}
async function configRoundtripReconnect(expected){
 configRoundtripClose();await sleep(1500);const deadline=performance.now()+60000;
 for(;;){
  try{const result=await configRoundtripRead();assert.equal(hex(result.snapshot.sha256),expected,'Complete configuration differs after restart');return result;}
  catch(error){configRoundtripClose();if(error.code==='ERR_ASSERTION'||performance.now()>deadline)throw error;await sleep(500);}
 }
}
try{
 let {author,snapshot}=await configRoundtripRead();
 if(!job){
  assert.ok(args.includes('--run'),'Verification requires an existing checkpoint');
  const original=Object.fromEntries(fields.map(key=>[key,snapshot.document.get(key).value]));
  assert.equal(typeof original[fields[0]],'string');assert.equal(typeof original[fields[1]],'boolean');assert.ok(Number.isInteger(original[fields[2]]));
  const target={
   [fields[0]]:original[fields[0]]==='USB Config Probe'?'USB Config Check':'USB Config Probe',
   [fields[1]]:!original[fields[1]],
   [fields[2]]:original[fields[2]]===30000?60000:30000
  };
  for(const key of fields)snapshot.document.set(key,target[key]);
  job={identity:author.fingerprint,generation:author.generation,session:hex(connection.session.id),original,target,originalHash:hex(snapshot.sha256),targetHash:hex(new Uint8Array(await crypto.subtle.digest('SHA-256',snapshot.document.serialize({kind:'working'})))),targetUpload:hex(random()),restoreUpload:hex(random()),phase:'ready'};
  await persist();for(const key of fields)snapshot.document.set(key,original[key]);
 }
 assert.deepEqual(Object.keys(job.original),fields);assert.deepEqual(Object.keys(job.target),fields);
 if(args.includes('--verify')){
  assert.ok(['restoring','restored'].includes(job.phase),'Original restoration has not been submitted');assert.equal(hex(snapshot.sha256),job.originalHash);
  for(const key of fields)assert.equal(snapshot.document.get(key).value,job.original[key]);
  if(job.phase==='restoring'){job.phase='restored';await persist();}
  console.log(JSON.stringify({readOnly:true,originalRestored:true,completeConfigurationHashVerified:true,identityUnchanged:true,fields:snapshot.document.size,testedFields:fields}));
 }else{
  if(job.phase==='ready'){
   assert.equal(hex(snapshot.sha256),job.originalHash,'Configuration changed before test');
   for(const key of fields)snapshot.document.set(key,job.target[key]);
   job.phase='writing';await persist();
   const receipt=await saveConfigDocument(connection.session,snapshot.document,snapshot.sha256,{id:bin(job.targetUpload)});
   console.log(JSON.stringify({targetCommitAccepted:true,restartPending:receipt.restartPending,testedFields:fields}));
  }
  if(job.phase==='writing'){
   // Lost commit replies are resolved only by reading. Never blindly repeat 21.
   ({author,snapshot}=await configRoundtripReconnect(job.targetHash));
   for(const key of fields)assert.equal(snapshot.document.get(key).value,job.target[key]);
   job.phase='target_verified';await persist();console.log(JSON.stringify({targetReadBack:true,completeConfigurationHashVerified:true}));
  }
  if(job.phase==='target_verified'){
   assert.equal(hex(snapshot.sha256),job.targetHash,'Configuration changed before restoration');
   for(const key of fields)snapshot.document.set(key,job.original[key]);
   job.phase='restoring';await persist();
   const receipt=await saveConfigDocument(connection.session,snapshot.document,snapshot.sha256,{id:bin(job.restoreUpload)});
   console.log(JSON.stringify({originalRestoreAccepted:true,restartPending:receipt.restartPending}));
  }
  if(job.phase==='restoring'){
   ({author,snapshot}=await configRoundtripReconnect(job.originalHash));
   for(const key of fields)assert.equal(snapshot.document.get(key).value,job.original[key]);
   job.phase='restored';await persist();
  }
  assert.equal(job.phase,'restored');assert.equal(hex(snapshot.sha256),job.originalHash);
  console.log(JSON.stringify({configurationRoundtripVerified:true,originalRestored:true,completeConfigurationHashVerified:true,identityUnchanged:true,fields:snapshot.document.size,testedFields:fields}));
 }
 await connection.session.request(3,[]);
}finally{configRoundtripClose();}
