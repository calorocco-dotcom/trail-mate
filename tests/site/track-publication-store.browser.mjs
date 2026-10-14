import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import assert from 'node:assert/strict';
import http from 'node:http';
const bundle=await build({stdin:{contents:"import * as store from './site/tracks/src/draft-store.js'; import {encodeTrackManifest} from './site/tracks/src/manifest.js'; import {createTrackPublicationPlan} from './site/tracks/src/publication-create.js'; import {prepareSavedTrackDraft} from './site/tracks/src/prepare-saved-draft.js'; import {commitSavedTrackDraft} from './site/tracks/src/commit-saved-draft.js'; import {querySavedTrackDraft} from './site/tracks/src/query-saved-draft.js'; import {decode} from './site/geocaching/src/protocol.js'; globalThis.fixture={...store,encodeTrackManifest,createTrackPublicationPlan,prepareSavedTrackDraft,commitSavedTrackDraft,querySavedTrackDraft,decode};",resolveDir:process.cwd()},bundle:true,write:false,format:'iife',platform:'browser'});
const server=http.createServer((req,res)=>res.end('<!doctype html><title>Storage test</title>'));
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
try{
 const page=await browser.newPage();await page.goto(`http://127.0.0.1:${server.address().port}`);await page.addScriptTag({content:bundle.outputFiles[0].text});
 const result=await page.evaluate(async()=>{
  const s=globalThis.fixture,bin=(n,v)=>new Uint8Array(n).fill(v),source=new TextEncoder().encode('<kml/>');
  const row=await s.saveTrackDraft(source,{name:'存储测试'}),fileHash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
  const plan={operationId:bin(16,1),kmlUploadId:bin(16,2),manifestUploadId:bin(16,3),generation:7,fileHash,fileBytes:source.length,
   manifest:s.encodeTrackManifest([1,bin(64,4),bin(16,5),0,1,null,0,100,100,'t','',[0,0,0,0],[0,null,null,1],[fileHash,source.length,'application/vnd.google-earth.kml+xml',1]])};
  await s.saveTrackDraftPublicationPlan(row.id,row.version,plan);await s.saveTrackDraftPublicationPlan(row.id,row.version,plan);
  const hash=async(...parts)=>{const input=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let offset=0;for(const p of parts){input.set(p,offset);offset+=p.length;}return new Uint8Array(await crypto.subtle.digest('SHA-256',input));};
  const binding={operationId:plan.operationId,generation:7,revision:1,directory:bin(16,8),directoryEpoch:bin(16,9),fileHash,fileBytes:source.length,
   authorHash:await hash(bin(64,4)),objectId:await hash(new TextEncoder().encode('TrailMate.Track.ID.v1\0'),bin(64,4),bin(16,5)),
   revisionHash:await hash(new TextEncoder().encode('TrailMate.Track.Revision.v1\0'),plan.manifest)};
  await s.saveTrackDraftJobBinding(row.id,row.version,binding);await s.saveTrackDraftJobBinding(row.id,row.version,binding);
  const bound=await s.readTrackDraft(row.id);if(!(bound.jobBinding instanceof ArrayBuffer))throw Error('job binding missing');
  let retarget=false;try{await s.saveTrackDraftJobBinding(row.id,row.version,{...binding,directoryEpoch:bin(16,10)});}catch{retarget=true;}
  if(!retarget)throw Error('job binding retarget accepted');
  let wrongAuthor=false;try{await s.saveTrackDraftJobBinding(row.id,row.version,{...binding,authorHash:bin(32,10)});}catch{wrongAuthor=true;}
  if(!wrongAuthor)throw Error('job binding wrong author accepted');
  const commitPrepared={...binding,prepared:true},commitDirectory={capabilitiesVerified:true,protocol:'trailmate.track.v1',delivery:binding.directory,epoch:binding.directoryEpoch},commitAuthor={verified:true,generation:7,publicKey:bin(64,4)};
  let traffic=0,lost=false;
  try{await s.commitSavedTrackDraft({async request(op){++traffic;if(op!==83||!(await s.readTrackDraft(row.id)).jobBinding)throw Error('commit before persistence');throw Error('lost response');}},row.id,row.version,commitPrepared,commitDirectory,commitAuthor);}catch(error){lost=error.code==='COMMIT_PENDING'&&error.operationId[0]===1;}
  if(!lost||traffic!==1)throw Error('lost commit binding not retained');
  const accepted=await s.commitSavedTrackDraft({async request(op,body){if(op!==83)throw Error('wrong operation');return [...body,1];}},row.id,row.version,commitPrepared,commitDirectory,commitAuthor);
  if(!accepted.localAdmitted||accepted.published||accepted.directoryConfirmed)throw Error('local admission treated as publication');
  const confirmed=await s.querySavedTrackDraft({async request(op,body){if(op!==48||body[0][0]!==1||body[1]!==7||body[2]!==1)throw Error('wrong retained query');
   return [binding.operationId,binding.objectId,binding.revisionHash,7,1,binding.directory,binding.directoryEpoch,binding.fileHash,binding.fileBytes,binding.fileBytes,4,2,binding.authorHash,0];}},row.id,row.version,commitAuthor);
  if(!confirmed.published||!confirmed.directoryConfirmed)throw Error('retained query did not confirm');
  let staleQuery=false;try{await s.querySavedTrackDraft({async request(){throw Error('unexpected traffic');}},row.id,row.version,{...commitAuthor,generation:8});}catch(error){staleQuery=error.message.includes('设备身份已变化');}
  if(!staleQuery)throw Error('stale retained identity accepted');
  let conflict=false;try{await s.saveTrackDraftPublicationPlan(row.id,row.version,{...plan,operationId:bin(16,9)});}catch{conflict=true;}
  const persisted=await s.readTrackDraft(row.id);
  const next=await s.saveTrackDraft(source,{...row,expectedVersion:row.version});
  let stale=false;try{await s.saveTrackDraftPublicationPlan(row.id,row.version,plan);}catch{stale=true;}
  const current=await s.readTrackDraft(row.id);
  const editedHistory=await s.listTrackJobHistory();if(editedHistory.length!==1||editedHistory[0].draftId!==row.id)throw Error('edit discarded job history');
  await s.removeTrackDraft(row.id,next.version);
  const deletedHistory=await s.listTrackJobHistory();if(deletedHistory.length!==1||!(deletedHistory[0].binding instanceof ArrayBuffer))throw Error('delete discarded job history');
  globalThis.migrationRecord=deletedHistory[0];
  const realKml=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:gx="http://www.google.com/kml/ext/2.2"><Placemark><gx:Track><altitudeMode>absolute</altitudeMode><when>2026-10-11T00:00:00Z</when><when>2026-10-11T00:01:00Z</when><gx:coord>120 30 100</gx:coord><gx:coord>120.01 30.01 125</gx:coord></gx:Track></Placemark></kml>');
  let counter=1;
  const generated=await s.createTrackPublicationPlan(realKml,{verified:true,publicKey:bin(64,4),generation:7},
    {title:'真实几何',createdAt:100,randomBytes:n=>bin(n,counter++)});
  const record=s.decode(generated.manifest,4096),realRow=await s.saveTrackDraft(realKml,{name:'真实几何'});
  await s.saveTrackDraftPublicationPlan(realRow.id,realRow.version,generated);
  const generatedStored=await s.readTrackDraft(realRow.id);await s.removeTrackDraft(realRow.id,realRow.version);
  const retryRow=await s.saveTrackDraft(realKml,{name:'断线重试'}),author={verified:true,publicKey:bin(64,4),generation:7};
  let failedAfterSave=false;
  try{await s.prepareSavedTrackDraft({async request(){const stored=await s.readTrackDraft(retryRow.id);failedAfterSave=stored.publicationPlan instanceof ArrayBuffer;throw Error('disconnected');}},retryRow.id,retryRow.version,author);}catch(error){if(error.message!=='disconnected')throw error;}
  const retained=s.decode(new Uint8Array((await s.readTrackDraft(retryRow.id)).publicationPlan),4352);
  let transfer;
  const fakeSession={async request(op,body){
   if(op===32){transfer={id:body[0],total:body[2],offset:0};return [transfer.id,320,0,0];}
   if(op===33){transfer.offset+=body[2].length;return [transfer.id,320,transfer.offset,0];}
   if(op===35)return [transfer.id,320,transfer.total,1];
   if(op!==82||body[0].some((v,i)=>v!==retained[1][i]))throw Error('changed operation');
   const raw=retained[5],record=s.decode(raw,4096),hash=async bytes=>new Uint8Array(await crypto.subtle.digest('SHA-256',bytes));
   const join=(domain,...parts)=>{const prefix=new TextEncoder().encode(domain+'\0'),out=new Uint8Array(prefix.length+parts.reduce((n,p)=>n+p.length,0));out.set(prefix);let pos=prefix.length;for(const part of parts){out.set(part,pos);pos+=part.length;}return out;};
   return [body[0],1,await hash(join('TrailMate.Track.ID.v1',record[1],record[2])),await hash(join('TrailMate.Track.Revision.v1',raw)),1];
  }};
  const preparing=s.prepareSavedTrackDraft(fakeSession,retryRow.id,retryRow.version,author);
  author.publicKey.fill(9);author.generation=99;author.verified=false;
  const resumed=await preparing;
  await s.removeTrackDraft(retryRow.id,retryRow.version);
  return {conflict,stale,persisted:persisted.publicationPlan instanceof ArrayBuffer,cleared:current.publicationPlan===undefined,
    bounds:record[11],points:record[12][3],distance:record[12][0],duration:record[12][1],ascent:record[12][2],generatedStored:generatedStored.publicationPlan instanceof ArrayBuffer,
    failedAfterSave,resumed:resumed.prepared&&!resumed.published};
 });
 assert.deepEqual(result.bounds,[1200000000,300000000,1200100000,300100000]);
 assert.equal(result.points,2);assert.ok(result.distance>1000);assert.equal(result.generatedStored,true);
 assert.equal(result.duration,60);assert.equal(result.ascent,25);
 assert.equal(result.failedAfterSave,true);assert.equal(result.resumed,true);
 for(const key of ['conflict','stale','persisted','cleared'])assert.equal(result[key],true);
 await page.evaluate(async()=>{
  const record=globalThis.migrationRecord;
  await new Promise((resolve,reject)=>{const request=indexedDB.deleteDatabase('trailmate-track-drafts');request.onsuccess=resolve;request.onerror=()=>reject(request.error);});
  const db=await new Promise((resolve,reject)=>{const request=indexedDB.open('trailmate-track-drafts',1);
   request.onupgradeneeded=()=>{request.result.createObjectStore('metadata',{keyPath:'id'});request.result.createObjectStore('files',{keyPath:'id'});};
   request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);});
  await new Promise((resolve,reject)=>{const tx=db.transaction('metadata','readwrite');tx.oncomplete=resolve;tx.onabort=()=>reject(tx.error);
   tx.objectStore('metadata').put({id:record.draftId,version:record.draftVersion,name:record.name,updatedAt:record.createdAt,jobBinding:record.binding});});db.close();
  const migrated=await globalThis.fixture.listTrackJobHistory();
  if(migrated.length!==1||migrated[0].draftId!==record.draftId||new Uint8Array(migrated[0].binding).some((byte,i)=>byte!==new Uint8Array(record.binding)[i]))throw Error('legacy job history migration failed');
  const changed=new Uint8Array(record.binding).slice();changed[5]^=1;let conflict=false;
  try{await globalThis.fixture.removeTrackJobHistory(record.key,changed);}catch(error){conflict=error.code==='HISTORY_CHANGED';}
  if(!conflict||(await globalThis.fixture.listTrackJobHistory()).length!==1)throw Error('changed history deletion accepted');
  let invalid=false;try{await globalThis.fixture.importTrackJobHistory(new Uint8Array(32));}catch(error){invalid=error.code==='HISTORY_INVALID';}
  if(!invalid||(await globalThis.fixture.listTrackJobHistory()).length!==1)throw Error('invalid history import changed stored records');
 });
 console.log('Track publication plan IndexedDB test passed');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
