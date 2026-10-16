// Installs one isolated, foreign-author, signed KML/PNG fixture; never publishes.
// --query verifies persistent disk state after a new connection without uploading.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import {readFile,writeFile} from 'node:fs/promises';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encodeFrame,FrameDecoder,FRAME,decodeHello} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {encode,equal} from '../../site/geocaching/src/protocol.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {installDeviceTrackPackage} from '../../site/tracks/src/device-package-install.js';
import {queryInstalledDevicePackage} from '../../site/tracks/src/device-package-query.js';

const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1],hex=value=>Buffer.from(value).toString('hex'),unhex=value=>Uint8Array.from(Buffer.from(value,'hex'));
if(!args.includes('--port')||!args.includes('--python')||!args.includes('--checkpoint'))throw Error('Use --port --python --checkpoint [--query]');
const child=spawn(option('--python'),[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),option('--port')],{stdio:['pipe','pipe','inherit'],windowsHide:true});let pending,sequence=0;
createInterface({input:child.stdout}).on('line',line=>{const reply=JSON.parse(line);if(pending){const current=pending;pending=null;reply.error?current.reject(Error(reply.error)):current.resolve(reply);}});
child.on('exit',code=>{if(pending){pending.reject(Error('Serial bridge exited '+code));pending=null;}});
async function exchange(type,payload,response){
 assert.ok(!pending);const next=++sequence;
 const reply=await new Promise((resolve,reject)=>{pending={resolve,reject};child.stdin.write(JSON.stringify({hex:hex(encodeFrame(type,next,payload)),sequence:next,response})+'\n');});
 const frame=new FrameDecoder().push(unhex(reply.hex))[0];assert.ok(frame&&frame.type===response&&frame.sequence===next);return frame.payload;
}
try{
 console.log(JSON.stringify({handshake:decodeHello(await exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK))}));
 const calls=[],session={id:crypto.getRandomValues(new Uint8Array(16)),async request(operation,body){
  if(![0,1,2,3,32,33,35,100,101].includes(operation))throw Error('Non-isolated package operation');calls.push(operation);
  const id=crypto.getRandomValues(new Uint8Array(16)),response=decodeManagement(await exchange(FRAME.MANAGEMENT_REQUEST,encodeManagement(this.id,id,operation,body),FRAME.MANAGEMENT_RESPONSE));
  assert.ok(equal(response.session,this.id)&&equal(response.request,id));if(response.code)throw Object.assign(Error('Device status '+response.code+' for '+operation),{code:response.code});return response.body;
 }};
 const capabilities=await session.request(0,[]),operations=capabilities[4],author=await proveDeviceAuthor(session),id=()=>crypto.getRandomValues(new Uint8Array(16));
 if(args.includes('--query')){
  const checkpoint=JSON.parse(await readFile(option('--checkpoint'),'utf8'));assert.equal(hex(author.publicKey),checkpoint.deviceAuthor);
  const result=await queryInstalledDevicePackage(session,{author,operationId:id(),objectId:unhex(checkpoint.objectId),revisionHash:unhex(checkpoint.revisionHash)},{operations,timeoutMs:90000,retryMs:250});
  assert.ok(result.installed&&!result.missing&&result.target===checkpoint.target);assert.equal(calls.some(op=>[32,33,35,100].includes(op)),false);
  console.log(JSON.stringify({persistentPackageVerified:true,target:result.target,originalGeneration:checkpoint.authorGeneration,currentGeneration:author.generation,uploads:0,installRequests:0}));
 }else{
  const remote=await Identity.generate(),publicKey=await remote.getPublicKey();assert.ok(!equal(publicKey,author.publicKey));
  const kml=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Document><name>USB package verification</name><description><![CDATA[<img src="photos/p.png"/>]]></description><Placemark><LineString><coordinates>1,2,0 1.001,2.001,0</coordinates></LineString></Placemark></Document></kml>');
  const photo=unhex('89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000b4944415478da636000020000050001a5f645400000000049454e44ae426082');
  const kmlHash=await Identity.fullHash(kml),photoHash=await Identity.fullHash(photo),index=encode([1,kmlHash,kml.length,[['photo',photoHash,photo.length,'image/png','photos/p.png',0,1]]]),timestamp=Math.floor(Date.now()/1000);
  const manifest=encodeTrackManifest([2,publicKey,id(),0,1,null,0,timestamp,timestamp,'USB package verification','Isolated hardware test; not published.',[10000000,20000000,10010000,20010000],[157,null,null,2],[kmlHash,kml.length,'application/vnd.google-earth.kml+xml',1],[0,[],0,0,0,0,0,null],[await Identity.fullHash(index),index.length,1,1]]);
  const signed=[manifest,await remote.sign(trackManifestSigningBytes(manifest))],verified=await verifyTrackManifest(signed),input={author,kml,signed,objectId:verified.objectId,revisionHash:verified.revisionHash,operationId:id(),kmlUploadId:id(),signedUploadId:id(),attachmentIndex:index,attachmentFiles:[{id:'photo',content:photo}],attachmentUploadIds:[{id:'photo',uploadId:id()}],indexUploadId:id(),packageUploadId:id()};
  const target='/routes/p-'+hex(verified.revisionHash)+'.kml';await writeFile(option('--checkpoint'),JSON.stringify({deviceAuthor:hex(author.publicKey),authorGeneration:author.generation,objectId:hex(verified.objectId),revisionHash:hex(verified.revisionHash),target}));
  let previous;const result=await installDeviceTrackPackage(session,input,{operations,timeoutMs:90000,retryMs:250,onProgress:event=>{if(previous!==event.phase){previous=event.phase;console.log(JSON.stringify({phase:event.phase}));}}});
  assert.ok(result.installed&&result.target===target);assert.deepEqual(result.signed,signed);assert.equal(calls.some(op=>[71,96,99].includes(op)),false);
  console.log(JSON.stringify({foreignAuthorPackageInstalled:true,target,PNGBytes:photo.length,originalSignaturePreserved:true,deviceContentSignatures:0,networkPublications:0}));
 }
 await session.request(3,[]);
}finally{child.stdin.end();}
