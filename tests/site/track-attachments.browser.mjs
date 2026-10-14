import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import {readFile,mkdir} from 'node:fs/promises';
import http from 'node:http';
import assert from 'node:assert/strict';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {encode,decode,equal} from '../../site/geocaching/src/protocol.js';
import {signDeviceTrackManifest} from '../../site/tracks/src/device-local-signature.js';
import {verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {uploadTrackAttachmentResource} from '../../site/tracks/src/device-upload.js';
import {Identity as NativeIdentity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {locales} from '../../site/i18n/registry.js';
import {trackAttachmentsText} from '../../site/i18n/track-attachments-labels.js';
const contents=`import React from 'react';import {createRoot} from 'react-dom/client';
import 'animal-island-ui/style';import './site/styles.css';import './site/tracks/src/tracks.css';
import {TrackAttachments} from './site/tracks/src/track-attachments.jsx';
import {ManagementCopyProvider} from './site/i18n/management-copy.jsx';
import {readTrackDraft,saveTrackDraft,saveTrackDraftAttachments,saveTrackDraftPublicationPlan,removeTrackDraft} from './site/tracks/src/draft-store.js';
import {draftPackageInputs,draftPackageCreationOptions} from './site/tracks/src/draft-package.js';
import {createTrackPublicationPlan} from './site/tracks/src/publication-create.js';
import {parseKml} from './site/tracks/src/kml.js';import {encode,decode,equal} from './site/geocaching/src/protocol.js';
import {Identity} from '@reticulum/core';
import {decodeTrackPublicationPlan} from './site/tracks/src/publication-plan.js';
import {reviewTrackIPPublication,signTrackIPPublication,publishTrackIPPublication} from './site/tracks/src/ip-publication-workflow.js';
import {loadTrackIPCheckpoint} from './site/tracks/src/ip-publication-journal.js';
const source=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><name>Photo route</name><description><![CDATA[<p>View</p><img src="images/photo.jpg">]]></description><LineString><coordinates>120,30 120.01,30.01</coordinates></LineString></Placemark></kml>');
const root=createRoot(document.getElementById('root'));let draft,locale='zh-Hans';
const render=()=>{globalThis.version=draft.version;root.render(<ManagementCopyProvider locale={locale}><div className="page-shell tracks-shell"><main className="main-content"><TrackAttachments draft={draft} model={parseKml(source)} onSaved={async row=>{draft=await readTrackDraft(row.id);render();}} onDirty={value=>globalThis.dirty=value}/></main></div></ManagementCopyProvider>);};
globalThis.start=async()=>{
 if(!sessionStorage.getItem('seeded')){
  const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',source)),b=>b.toString(16).padStart(2,'0')).join('');
  await new Promise((resolve,reject)=>{const request=indexedDB.open('trailmate-track-drafts',2);request.onupgradeneeded=()=>{for(const name of ['metadata','files','jobs'])request.result.createObjectStore(name,{keyPath:name==='jobs'?'key':'id'});};request.onerror=()=>reject(request.error);request.onsuccess=()=>{const db=request.result,tx=db.transaction(['metadata','files','jobs'],'readwrite');tx.objectStore('metadata').put({id:'legacy',version:1,bytes:source.length,hash,name:'Photo route',filename:'route.kml',updatedAt:1});tx.objectStore('files').put({id:'legacy',bytes:source.buffer});tx.objectStore('jobs').put({key:'retained-job',evidence:'unchanged'});tx.oncomplete=()=>{db.close();resolve();};};});sessionStorage.setItem('seeded','1');
 }
 draft=await readTrackDraft('legacy');globalThis.version=draft.version;render();
};
globalThis.setLocale=value=>{locale=value;render();};
globalThis.snapshot=()=>readTrackDraft('legacy');
globalThis.packageTest=async nativeKey=>{
 const snapshot=await readTrackDraft('legacy'),identity=await Identity.generate(),author={verified:true,publicKey:nativeKey?new Uint8Array(nativeKey):await identity.getPublicKey(),generation:1};
 const options=await draftPackageCreationOptions(snapshot),plan=await createTrackPublicationPlan(snapshot.source,author,{...options,title:snapshot.name});
 await saveTrackDraftPublicationPlan(snapshot.id,snapshot.version,plan);
 const first=await draftPackageInputs(snapshot,plan),retained=await readTrackDraft(snapshot.id),second=await draftPackageInputs(retained,plan);
 if(!equal(first.indexUploadId,second.indexUploadId)||!equal(first.packageUploadId,second.packageUploadId)||!equal(first.attachmentUploadIds[0].uploadId,second.attachmentUploadIds[0].uploadId))throw Error('Unstable retry IDs');
 return {version:decode(plan.manifest,4096)[0],count:decode(first.attachmentIndex,294912)[3].length,bytes:first.attachmentFiles[0].content.length,planBytes:retained.publicationPlan.byteLength};
};
globalThis.exportPackage=async()=>{const snapshot=await readTrackDraft('legacy'),plan=decodeTrackPublicationPlan(new Uint8Array(snapshot.publicationPlan)),packet=await draftPackageInputs(snapshot,plan),hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');return {plan:Object.fromEntries(Object.entries(plan).map(([key,value])=>[key,value instanceof Uint8Array?hex(value):value])),kml:hex(snapshot.source),index:hex(packet.attachmentIndex),files:packet.attachmentFiles.map(row=>({id:row.id,content:hex(row.content)})),ids:packet.attachmentUploadIds.map(row=>({id:row.id,uploadId:hex(row.uploadId)})),indexId:hex(packet.indexUploadId),packageId:hex(packet.packageUploadId)};};
globalThis.ipTest=async nativeKey=>{
 const snapshot=await readTrackDraft('legacy'),account={verified:true,publicKey:new Uint8Array(nativeKey),generation:1},directory=await Identity.generate(),directoryKey=await directory.getPublicKey(),hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join(''),join=(...parts)=>{const out=new Uint8Array(parts.reduce((sum,p)=>sum+p.length,0));let offset=0;for(const part of parts){out.set(part,offset);offset+=part.length;}return out;};
 const delivery=await Identity.truncatedHash(join((await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),directory.identityHash)),epoch=new Uint8Array(16).fill(5),key=hex(delivery);
 const entry={ready:true,identity:directory,epoch,destination:delivery,operations:[]},selected={key,capabilitiesVerified:true,publicKey:directoryKey,epoch,delivery};let publishes=0,forwarded=false;
 const network={directories:new Map([[key,entry]]),publishTrack:async(target,signed,source,options)=>{
  publishes++;const fields=decode(signed[0],4096),index=decode(options.attachmentIndex,294912);forwarded=fields[0]===2&&options.attachmentFiles.length===1&&index[3][0][0]===options.attachmentFiles[0].id&&equal(await Identity.fullHash(options.attachmentFiles[0].content),index[3][0][1]);
  if(!forwarded)throw Error('Attachments not forwarded');
  if(publishes===1)throw Error('Receipt lost');
  const verified=await import('./site/tracks/src/manifest.js').then(module=>module.verifyTrackManifest(signed)),receipt=encode([1,options.requestId,verified.objectId,verified.revisionHash,fields[4],fields[6],fields[13][0],source.length,directoryKey,delivery,epoch,1,Math.floor(Date.now()/1000)]);
  return {directoryConfirmed:true,signed:[receipt,await directory.sign(join(new TextEncoder().encode('TrailMate.Track.Receipt.v1\\0'),receipt))]};
 }};
 let unsupported=false;try{await reviewTrackIPPublication(snapshot.id,snapshot.version,account,selected,network);}catch(error){unsupported=error.code==='TRACK_PACKAGE_DIRECTORY';}
 if(!unsupported)throw Error('Old directory accepted attachments');entry.operations=[14];
 const reviewed=await reviewTrackIPPublication(snapshot.id,snapshot.version,account,selected,network),session={request:async(op,body)=>decode(Uint8Array.from((await nativeWireRequest(op,hex(encode(body)))).match(/../g),byte=>parseInt(byte,16)),471)};
 const signed=await signTrackIPPublication(session,reviewed,account,{operations:[32,33,35,99]});let blocked=false;
 try{await saveTrackDraftAttachments(snapshot.id,snapshot.version,snapshot.attachmentRows);}catch(error){blocked=error.code==='TRACK_IP_DRAFT_LOCKED';}if(!blocked)throw Error('Signed draft remained editable');
 try{await publishTrackIPPublication(network,signed,account);throw Error('Lost receipt accepted');}catch(error){if(error.message!=='Receipt lost')throw error;}
 const retained=loadTrackIPCheckpoint(account.publicKey,snapshot.id,snapshot.version),confirmed=await publishTrackIPPublication(network,retained,account);
 return {stage:confirmed.stage,publishes,forwarded,requestSame:equal(signed.requestId,retained.requestId)&&equal(confirmed.requestId,retained.requestId)};
};
globalThis.invalidTest=async()=>{const snapshot=await readTrackDraft('legacy');let errors=[];
 for(const rows of [[...snapshot.attachmentRows,{...snapshot.attachmentRows[0],id:snapshot.attachmentRows[0].id.toUpperCase(),reference:'other.jpg'}],[{...snapshot.attachmentRows[0],reference:'../photo.jpg'}]]){
  try{await saveTrackDraftAttachments(snapshot.id,snapshot.version,rows);throw Error('Bad rows accepted');}catch(error){errors.push(error.code);}
 }
 return errors;
};
globalThis.casTest=async()=>{const old=await readTrackDraft('legacy');await saveTrackDraftAttachments(old.id,old.version,old.attachmentRows);try{await saveTrackDraftAttachments(old.id,old.version,[]);throw Error('Stale CAS accepted');}catch(error){if(error.code!=='DRAFT_CONFLICT')throw error;}const next=await readTrackDraft(old.id);return {version:next.version,count:next.attachmentRows.length,invalidated:!next.publicationPlan&&!next.attachmentBindings};};
globalThis.editTest=async()=>{const before=await readTrackDraft('legacy');await saveTrackDraft(before.source,{id:before.id,expectedVersion:before.version,name:'Renamed'});const after=await readTrackDraft(before.id);return {count:after.attachmentRows.length,bytes:after.attachmentBytes,same:equal(before.attachmentRows[0].content,after.attachmentRows[0].content)};};
globalThis.corruptTest=async()=>{const before=await readTrackDraft('legacy');await new Promise(resolve=>{const request=indexedDB.open('trailmate-track-drafts');request.onsuccess=()=>{const db=request.result,tx=db.transaction('attachments','readwrite'),store=tx.objectStore('attachments'),get=store.get('legacy');get.onsuccess=()=>{const row=get.result;new Uint8Array(row.rows[0].content)[0]^=1;store.put(row);};tx.oncomplete=()=>{db.close();resolve();};};});try{await readTrackDraft('legacy');throw Error('Corruption accepted');}catch(error){return error.code;}};
globalThis.removeTest=async()=>{const request=indexedDB.open('trailmate-track-drafts'),db=await new Promise(resolve=>request.onsuccess=()=>resolve(request.result));const tx=db.transaction('metadata'),get=tx.objectStore('metadata').get('legacy'),row=await new Promise(resolve=>get.onsuccess=()=>resolve(get.result));db.close();await removeTrackDraft('legacy',row.version);const again=indexedDB.open('trailmate-track-drafts'),check=await new Promise(resolve=>again.onsuccess=()=>resolve(again.result));const result=await new Promise(resolve=>{const tx=check.transaction(['attachments','jobs']),assets=tx.objectStore('attachments').get('legacy'),jobs=tx.objectStore('jobs').count();tx.oncomplete=()=>resolve({empty:assets.result===undefined,jobs:jobs.result});});check.close();return result;};`;
const bundle=await build({stdin:{contents,resolveDir:process.cwd(),loader:'jsx'},nodePaths:[path.resolve('site/node_modules')],bundle:true,write:false,format:'iife',platform:'browser',outdir:'.codex-build/ui-check',loader:{'.svg':'dataurl','.png':'dataurl','.webp':'dataurl','.woff':'dataurl','.woff2':'dataurl','.ttf':'dataurl'},logLevel:'silent'});
const script=bundle.outputFiles.find(file=>file.path.endsWith('.js')).text,css=bundle.outputFiles.find(file=>file.path.endsWith('.css')).text;
const server=http.createServer((req,res)=>{res.setHeader('Content-Type','text/html');res.end('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><div id="root"></div>');});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
const child=spawn(path.resolve('.codex-build/hostlink-codec/native/track_snapshot_owner'+(process.platform==='win32'?'.exe':'')),['package-protocol'],{stdio:['pipe','pipe','pipe']}),pending=[];
let stderr='';child.stderr.on('data',chunk=>stderr+=chunk);
const ready=new Promise((resolve,reject)=>pending.push({resolve,reject})),exit=new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',code=>code===0?resolve():reject(Error('Native package fixture failed: '+stderr)));});exit.catch(()=>{});
createInterface({input:child.stdout}).on('line',line=>{const wait=pending.shift();if(!wait)return;const [code,hex]=line.split(' ');Number(code)===0?wait.resolve(new Uint8Array(Buffer.from(hex,'hex'))):wait.reject(Object.assign(Error('Native status '+code),{code:Number(code)}));});
const send=line=>new Promise((resolve,reject)=>{pending.push({resolve,reject});child.stdin.write(line+'\n');});
const session={request:async(op,body)=>decode(await send(op+' '+Buffer.from(encode(body)).toString('hex')),471)};
try{
 const page=await browser.newPage({viewport:{width:1200,height:1000}}),url=`http://127.0.0.1:${server.address().port}`;
 await page.exposeFunction('nativeWireRequest',async(op,wire)=>Buffer.from(await send(op+' '+wire)).toString('hex'));
 const load=async()=>{await page.goto(url);await page.addStyleTag({content:css});await page.addScriptTag({content:script});await page.evaluate(()=>start());await page.getByRole('button',{name:'添加文件',exact:true}).waitFor();await page.waitForFunction(()=>!document.querySelector('.tracks-attachments button').disabled);};
 await load();assert.equal(await page.evaluate(()=>version),1);
 const photo=await readFile('tests/hostlink/fixtures/png-cache/red-16x8.jpg');
 await page.locator('input[type=file]').setInputFiles({name:'photo.jpg',mimeType:'image/jpeg',buffer:photo});
 await page.getByText('附件修改尚未保存',{exact:true}).waitFor();assert.equal(await page.getByLabel('KML 引用',{exact:true}).inputValue(),'images/photo.jpg');
 await page.getByRole('button',{name:'保存附件',exact:true}).click();await page.waitForFunction(()=>!dirty&&version===2&&!document.querySelector('.tracks-attachments button').disabled);
 const nativeKey=await ready,result=await page.evaluate(key=>packageTest(key),Array.from(nativeKey));assert.equal(result.version,2);assert.equal(result.count,1);assert.equal(result.bytes,photo.length);
 const exported=await page.evaluate(()=>exportPackage()),binary=value=>new Uint8Array(Buffer.from(value,'hex'));
 const plan=Object.fromEntries(Object.entries(exported.plan).map(([key,value])=>[key,typeof value==='string'?binary(value):value]));
 const input={...plan,kml:binary(exported.kml),author:{verified:true,publicKey:nativeKey,generation:1},attachmentIndex:binary(exported.index),attachmentFiles:exported.files.map(row=>({id:row.id,content:binary(row.content)})),attachmentUploadIds:exported.ids.map(row=>({id:row.id,uploadId:binary(row.uploadId)})),indexUploadId:binary(exported.indexId),packageUploadId:binary(exported.packageId)};
 const signed=await signDeviceTrackManifest(session,input,{operations:[32,33,35,99],prepareOptions:{retryMs:1,timeoutMs:15000}}),verified=await verifyTrackManifest(signed.signed);
 assert.ok(equal(verified.signed?.[0]??signed.signed[0],input.manifest));assert.equal(verified.record[15][2],1);
 await send('close');const repeated=await signDeviceTrackManifest(session,input,{operations:[32,33,35,99],prepareOptions:{retryMs:1,timeoutMs:15000}});assert.deepEqual(repeated.signed,signed.signed);assert.deepEqual(decode(await send('state'),471),[1,1]);
 await load();assert.equal(await page.evaluate(()=>version),2);await page.getByLabel('KML 引用',{exact:true}).waitFor();assert.equal(await page.getByLabel('KML 引用',{exact:true}).inputValue(),'images/photo.jpg');
 assert.deepEqual(await page.evaluate(key=>ipTest(key),Array.from(nativeKey)),{stage:'confirmed',publishes:2,forwarded:true,requestSame:true});assert.deepEqual(decode(await send('state'),471),[1,1]);
 const unrelated=new Uint8Array(16).fill(238),photoBytes=new Uint8Array(photo),photoHash=await NativeIdentity.fullHash(photoBytes),cachedBody=[input.operationId,input.kmlUploadId,input.manifestUploadId,1,null,input.packageUploadId];
 await session.request(32,[unrelated,9,photo.length,photoHash]);assert.equal((await session.request(99,cachedBody))[1],1);assert.deepEqual(decode(await send('state'),471),[1,2]);
 await uploadTrackAttachmentResource(session,photoBytes,{id:unrelated});assert.equal((await session.request(99,cachedBody))[1],1);assert.deepEqual(decode(await send('state'),471),[1,2]);await send('close');assert.deepEqual(decode(await send('state'),471),[1,0]);
 await page.getByLabel('离线使用必需',{exact:true}).uncheck();await page.getByText('附件修改尚未保存',{exact:true}).waitFor();
 for(const locale of locales){const text=trackAttachmentsText(locale.id);await page.evaluate(id=>{document.documentElement.dir=id==='ar'?'rtl':'ltr';setLocale(id);},locale.id);await page.getByRole('heading',{name:text.title,exact:true}).waitFor();assert.equal(await page.getByLabel(text.required,{exact:true}).isChecked(),false);assert.equal(await page.getByLabel(text.reference,{exact:true}).inputValue(),'images/photo.jpg');assert.equal(await page.getByLabel(text.reference,{exact:true}).getAttribute('dir'),'ltr');}
 await page.evaluate(()=>{document.documentElement.dir='ltr';setLocale('zh-Hans');});await page.setViewportSize({width:390,height:844});
 await mkdir('.codex-build/ui-check',{recursive:true});await page.screenshot({path:'.codex-build/ui-check/track-attachments-mobile.png',fullPage:true});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);
 await page.setViewportSize({width:1200,height:1000});await page.screenshot({path:'.codex-build/ui-check/track-attachments-desktop.png',fullPage:true});
 assert.deepEqual(await page.evaluate(()=>invalidTest()),['ATTACHMENT_ID','ATTACHMENT_REFERENCE']);
 assert.deepEqual(await page.evaluate(()=>casTest()),{version:3,count:1,invalidated:true});
 const edited=await page.evaluate(()=>editTest());assert.equal(edited.count,1);assert.equal(edited.bytes,photo.length);assert.equal(edited.same,true);
 assert.equal(await page.evaluate(()=>corruptTest()),'DRAFT_CORRUPT');assert.deepEqual(await page.evaluate(()=>removeTest()),{empty:true,jobs:1});
 console.log('Attachment UI/storage passed legacy v2 migration, exact files/index, stable retry IDs, reload, 11 locales, mobile/RTL, CAS, invalidation, corruption and cleanup.');
}finally{await send('close');child.stdin.end();await exit;assert.equal(stderr,'');await browser.close();await new Promise(resolve=>server.close(resolve));}
