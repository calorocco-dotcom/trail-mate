// Explicit, narrowly scoped maintenance of an isolated unpublished acceptance
// draft. No config/key/RF command; never delete a saved draft or signed version.
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import fs from 'node:fs/promises';
import {encodeFrame,FrameDecoder,FRAME,decodeHello,CAPABILITY} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {readDeviceDraft} from '../../site/geocaching/src/device-maintenance.js';
import {readDeviceDraftPage} from '../../site/geocaching/src/device-draft-catalog.js';
import {readSignatureRecovery} from '../../site/geocaching/src/device-signature-recovery.js';
import {verifyPublicationReceipt} from '../../site/geocaching/src/publication-receipt.js';
import {beginDevicePublicationConfirmation,readDevicePublicationConfirmation} from '../../site/geocaching/src/device-confirmation-status.js';
import {equal,encode,verifySigned} from '../../site/geocaching/src/protocol.js';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
for(const name of ['--port','--python','--expected-author','--draft','--name'])if(!args.includes(name))throw Error('Explicit port, Python, author, isolated draft ID and name required');
if(!/^COM\d+$/.test(option('--port'))||!/^[a-f0-9]{64}$/.test(option('--expected-author'))||!/^[a-f0-9]{32}$/.test(option('--draft'))||!/^Wio网页验收-[a-z0-9]+$/.test(option('--name')))throw Error('Invalid isolated test binding');
const draftId=Uint8Array.from(Buffer.from(option('--draft'),'hex'));
const kind=args.includes('--confirmed-record')?6:5;
const resume=args.includes('--resume-confirmation');
if(resume&&(kind!==6||args.includes('--abort')))throw Error('Confirmation resume requires the exact accepted record and excludes cleanup');
const child=spawn(option('--python'),[fileURLToPath(new URL('./serial_transport.py',import.meta.url)),option('--port')],{stdio:['pipe','pipe','pipe'],windowsHide:true});
let pending,sequence=0,stderr='';child.stderr.on('data',data=>{stderr=(stderr+data).slice(-4096);});
const done=new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',code=>{pending?.reject(Error('Serial transport stopped'));pending=null;resolve(code);});});done.catch(()=>{});
const lines=createInterface({input:child.stdout});lines.on('line',line=>{try{const value=JSON.parse(line),current=pending;pending=null;if(current)value.error?current.reject(Error(value.error)):current.resolve(value);}catch(error){pending?.reject(error);pending=null;}});
async function exchange(type,payload,response){
 if(pending)throw Error('Concurrent serial exchange rejected');const next=++sequence,raw=encodeFrame(type,next,payload);
 const reply=await new Promise((resolve,reject)=>{pending={resolve,reject};child.stdin.write(JSON.stringify({hex:Buffer.from(raw).toString('hex'),sequence:next,response})+'\n');});
 const frames=new FrameDecoder().push(Uint8Array.from(Buffer.from(reply.hex,'hex'))),frame=frames[0];
 if(frames.length!==1||frame.type!==response||frame.sequence!==next)throw Error('Invalid serial frame');return frame.payload;
}
let abortAllowed=false,abortIssued=false,confirmationBinding=null,ownedSession=false;
const session={id:crypto.getRandomValues(new Uint8Array(16)),async request(op,body){
 if(![0,1,2,3,38,64,65,36,75,76].includes(op)||(op===65&&!equal(body[0],draftId))||(op===38&&(body[0]!==kind||body[1]!==null))||(op===64&&kind!==6))throw Error('Unrelated command rejected');
 if([75,76].includes(op)&&(!resume||!confirmationBinding||!equal(body[0],confirmationBinding.id)||(op===75&&(!equal(body[1],confirmationBinding.hash)||body[2]!==confirmationBinding.authorGeneration||!equal(body[3],confirmationBinding.receipt.raw)||!equal(body[4],confirmationBinding.receipt.signature)||!equal(body[5],confirmationBinding.receipt.publicKey)||!equal(body[6],confirmationBinding.receipt.epoch)))))throw Error('Unverified confirmation rejected');
 if(op===36){if(!abortAllowed||abortIssued)throw Error('Unverified or repeated abort rejected');abortIssued=true;}
 const requestId=crypto.getRandomValues(new Uint8Array(16)),reply=decodeManagement(await exchange(FRAME.MANAGEMENT_REQUEST,encodeManagement(this.id,requestId,op,body),FRAME.MANAGEMENT_RESPONSE));
 if(!equal(reply.session,this.id)||!equal(reply.request,requestId))throw Error('Management correlation mismatch');
 if(reply.code!==0)throw Object.assign(Error('Device status '+reply.code),{code:reply.code});return reply.body;
}};
try{
 const hello=decodeHello(await exchange(FRAME.HELLO,new Uint8Array(),FRAME.HELLO_ACK));
 if(!(hello.capabilities&(kind===5?CAPABILITY.SHARED_SIGNATURE_RECOVERY:CAPABILITY.SHARED_CONFIRMATION_RECOVERY)))throw Error('Running firmware does not support safe input recovery');
 const caps=await session.request(0,[]),author=await proveDeviceAuthor(session);
 if(author.fingerprint!==option('--expected-author'))throw Error('Unexpected device author');
 const snapshot=await readDeviceDraft(session,draftId,author,{timeoutMs:90000,retryMs:500}),draft=snapshot.draft;
 if(!snapshot.editable||draft.name!==option('--name')||draft.state!==0||draft.latitudeE7!==310000000||draft.longitudeE7!==1210000000)throw Error('Draft is not the isolated acceptance fixture');
 let expectedHash,expectedSize,accepted,receipt;
 if(kind===5){if(BigInt(draft.generation)!==1n||draft.baseHash!==null)throw Error('Unpublished acceptance fixture changed; preserved');}
 else{
  const signed=JSON.parse(await fs.readFile(option('--confirmed-record'),'utf8')).map(part=>Uint8Array.from(part)),verified=await verifySigned(signed),record=verified.record;
  if(BigInt(draft.generation)<2n||!equal(record[1],author.publicKey)||!equal(record[2],draftId)||record[3]!==1||record[5]!==0||record[8]!==option('--name')||!equal(verified.summary[2],draft.baseHash))throw Error('Accepted record does not match the device fixture');
  let page,row;
  for(let count=0;count<32&&!row;count++){page=await readDeviceDraftPage(session,author,page?{offset:page.nextOffset,catalogGeneration:page.catalogGeneration}:{});row=page.rows.find(row=>equal(row.id,draftId));if(!page.hasMore)break;}
  console.log(JSON.stringify({draft:option('--draft'),publication:row?.publication??null}));
  // An interrupted IP confirmation has not yet created the device request
  // ledger, so its projection can still be 0/0. The signed author/nonce,
  // reserved draft base, exact staging SHA and signed receipt bind the resume.
  if(!row||row.publication.latestRevision>1||row.publication.localChanges||!equal(row.baseHash,verified.summary[2])||(!resume&&(row.publication.latestRevision!==1||row.publication.confirmedRevision!==1||row.publication.pending)))throw Error('Device state does not match the exact accepted version');
  const source=encode(signed);expectedHash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));expectedSize=source.length;
  if(resume){accepted=signed;receipt=Object.fromEntries(Object.entries(JSON.parse(await fs.readFile(option('--resume-confirmation'),'utf8'))).map(([key,value])=>[key,Uint8Array.from(value)]));await verifyPublicationReceipt([receipt.raw,receipt.signature],accepted,receipt);}
 }
 await session.request(3,[]);
 const settings={capabilities:hello.capabilities,operations:caps[4],timeoutMs:15000,kind};
 const row=await readSignatureRecovery(session,author,settings);
 if(!row){console.log(JSON.stringify({pendingUnsignedInput:false,aborted:false,draft:option('--draft')}));}
 else{
  if(!equal(row.draftId,draftId))throw Error('Shared input belongs to another draft; preserved');
  if(kind===6&&(!equal(row.hash,expectedHash)||row.total!==expectedSize))throw Error('Confirmation input differs from accepted signed version; preserved');
  console.log(JSON.stringify({pendingStagingKind:kind,draft:option('--draft'),bytes:row.total,authorGeneration:row.authorGeneration}));
  if(resume){
   session.id=row.owner.slice();const proved=await proveDeviceAuthor(session);ownedSession=true;
   if(proved.fingerprint!==author.fingerprint||proved.generation!==author.generation)throw Error('Device author changed; input preserved');
   const checked=await readSignatureRecovery(session,proved,settings);
   if(!checked||!equal(checked.id,row.id)||!equal(checked.owner,row.owner)||!equal(checked.hash,expectedHash)||!equal(checked.draftId,draftId)||checked.total!==expectedSize)throw Error('Confirmation input changed; preserved');
   confirmationBinding={id:row.id.slice(),hash:expectedHash,authorGeneration:proved.generation,receipt};
   let result=await beginDevicePublicationConfirmation(session,{id:row.id,sha256:expectedHash,sealed:true,authorGeneration:proved.generation,receipt},proved);
   const deadline=performance.now()+90000;
   while(!result.confirmed){if(result.phase===2||performance.now()>=deadline)throw Error('Exact confirmation remains unconfirmed');await new Promise(resolve=>setTimeout(resolve,500));result=await readDevicePublicationConfirmation(session,result.binding,proved);}
   const remaining=await readSignatureRecovery(session,proved,settings);if(remaining)throw Error('Confirmed input is still active');
   console.log(JSON.stringify({resumed:true,confirmed:true,revision:result.binding.revision,terminalInputReusable:true,draft:option('--draft'),newSignature:false}));
  }
  if(args.includes('--abort')){
   session.id=row.owner.slice();const proved=await proveDeviceAuthor(session);
   if(proved.fingerprint!==author.fingerprint||proved.generation!==author.generation)throw Error('Device author changed; input preserved');
   const checked=await readSignatureRecovery(session,proved,settings);
   if(!checked||!equal(checked.id,row.id)||!equal(checked.owner,row.owner)||!equal(checked.hash,row.hash)||!equal(checked.draftId,draftId)||checked.total!==row.total)throw Error('Shared input changed; preserved');
   abortAllowed=true;const response=await session.request(36,[row.id]);
   if(!Array.isArray(response)||response.length)throw Error('Invalid cleanup response');
   const remaining=await readSignatureRecovery(session,proved,settings);
   if(remaining)throw Error('Cleanup not verified');
   console.log(JSON.stringify({aborted:true,onlyStagingKind:kind,savedDraftPreserved:true,confirmedVersionPreserved:kind===6}));
  }
 }
}finally{if(ownedSession)await session.request(3,[]).catch(()=>{});child.stdin.end();const code=await done;lines.close();if(process.env.TRAIL_MATE_SERIAL_DIAGNOSTICS==='1'&&stderr)console.error(stderr.trim());if(code!==0)throw Error('Serial transport failed: '+stderr);}
