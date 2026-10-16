import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import {encodeFrame,FrameDecoder,FRAME,decodeHello} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {readConfigSnapshot} from '../../site/device/src/config-snapshot.js';
import {readDeviceRadioIdentity} from '../../site/device/src/radio-identity.js';
import {equal} from '../../site/geocaching/src/protocol.js';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
if(!args.includes('--port')||!args.includes('--python'))throw Error('Usage: node tools/hostlink/probe.mjs --port COMxx --python <Python with pyserial>');
const child=spawn(option('--python'),[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),option('--port')],{stdio:['pipe','pipe','inherit'],windowsHide:true});
let pending,sequence=0;
const lines=createInterface({input:child.stdout});
lines.on('line',line=>{const reply=JSON.parse(line);if(pending){const active=pending;pending=null;reply.error?active.reject(Error(reply.error)):active.resolve(reply);}});
child.on('exit',code=>{if(pending){pending.reject(Error('Serial transport exited: '+code));pending=null;}});
async function exchange(type,payload,response){
 if(pending)throw Error('Probe exchanges must be sequential');
 const next=++sequence,raw=encodeFrame(type,next,payload);
 const reply=await new Promise((resolve,reject)=>{pending={resolve,reject};child.stdin.write(JSON.stringify({hex:Buffer.from(raw).toString('hex'),sequence:next,response})+'\n');});
 const frame=new FrameDecoder().push(Uint8Array.from(Buffer.from(reply.hex,'hex')))[0];
 if(!frame||frame.type!==response||frame.sequence!==next)throw Error('Invalid serial response');return frame.payload;
}
try{
 const hello=decodeHello(await exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK));
 console.log(JSON.stringify({handshake:hello}));
 const session={id:crypto.getRandomValues(new Uint8Array(16)),async request(operation,body){
  if(![0,1,2,6,7,17].includes(operation))throw Error('Read-only probe rejected operation');
  const id=crypto.getRandomValues(new Uint8Array(16)),reply=decodeManagement(await exchange(FRAME.MANAGEMENT_REQUEST,encodeManagement(this.id,id,operation,body),FRAME.MANAGEMENT_RESPONSE));
  if(!equal(reply.session,this.id)||!equal(reply.request,id))throw Error('Management correlation mismatch');
  if(reply.code!==0)throw Object.assign(Error('Device status '+reply.code),{code:reply.code});return reply.body;
 }};
 const capabilities=await session.request(0,[]);console.log(JSON.stringify({capabilities}));
 if(capabilities[4]?.includes(6))console.log(JSON.stringify({identityFormats:await session.request(6,[])}));
 if([1,2].every(op=>capabilities[4]?.includes(op))){try{const author=await proveDeviceAuthor(session);console.log(JSON.stringify({authorVerified:author.verified,fingerprint:author.fingerprint,generation:author.generation}));
  if(args.includes('--expect-author')&&author.fingerprint!==option('--expect-author'))throw Error('Author fingerprint differs from expected baseline');
  if(args.includes('--radio-info')&&capabilities[4].includes(7))for(const kind of [1,2]){const radio=await readDeviceRadioIdentity(session,kind,author,{operations:capabilities[4]});console.log(JSON.stringify({radioIdentityInfo:{kind,present:radio.present,generation:radio.generation,publicKey:radio.publicKey?Buffer.from(radio.publicKey).toString('hex'):null}}));}
 }catch(error){console.log(JSON.stringify({identityCheckFailed:true,code:error.code??null,error:error.message}));process.exitCode=1;}}
 if(capabilities[4]?.includes(17)){const snapshot=await readConfigSnapshot(session),sha256=Buffer.from(snapshot.sha256).toString('hex');console.log(JSON.stringify({configVerified:true,fields:snapshot.document.size,bytes:snapshot.document.originalBytes.length,meshProtocol:snapshot.document.get('protocol.active').value,sha256}));if(args.includes('--expect-config-sha')&&sha256!==option('--expect-config-sha'))throw Error('Configuration differs from expected baseline');}
}finally{child.stdin.end();}
