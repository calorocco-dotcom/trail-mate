import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import http from 'node:http';
import assert from 'node:assert/strict';
import path from 'node:path';
import {locales} from '../../site/i18n/registry.js';
import {trackPreparationText} from '../../site/i18n/track-preparation-labels.js';
import {trackPublishText} from '../../site/i18n/track-publish-labels.js';
const contents=`import React from 'react';import {createRoot} from 'react-dom/client';
import {Identity} from '@reticulum/core';import {TrackPreparation} from './site/tracks/src/track-preparation.jsx';
import {authorProofMessage} from './site/device/src/author-identity.js';
import {saveTrackDraft,readTrackDraft} from './site/tracks/src/draft-store.js';
import {decode} from './site/geocaching/src/protocol.js';
import {decodeTrackJobBinding} from './site/tracks/src/job-binding.js';
import {ManagementCopyProvider} from './site/i18n/management-copy.jsx';
globalThis.startFixture=async()=>{
 const identity=await Identity.generate(),publicKey=await identity.getPublicKey(),join=(...parts)=>{const out=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let pos=0;for(const p of parts){out.set(p,pos);pos+=p.length;}return out;};
 const domain=d=>new TextEncoder().encode(d+'\\0');
 const nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
 const destination=await Identity.truncatedHash(join(nameHash,identity.identityHash)),info=[publicKey,identity.identityHash,destination,7];
 const source=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>120,30 120.01,30.01</coordinates></LineString></Placemark></kml>');
 let draft=await saveTrackDraft(source,{name:'签名交互测试'});let transfer,locked=false;globalThis.serialCalls=0;
 const session={id:new Uint8Array(16).fill(3),async request(op,body){
  ++globalThis.serialCalls;
  if(op===1){if(globalThis.holdAuthor){globalThis.authorWaiting=true;await new Promise(resolve=>globalThis.resumeAuthor=resolve);globalThis.authorWaiting=false;}return info;}
  if(op===2)return [...info,await identity.sign(authorProofMessage(this.id,body[0],publicKey,7))];
  if(op===32){transfer={id:body[0],offset:0,total:body[2]};return [transfer.id,320,0,0];}
  if(op===33){transfer.offset+=body[2].length;return [transfer.id,320,transfer.offset,0];}
  if(op===35)return [transfer.id,320,transfer.total,1];
  if(op===83){const row=await readTrackDraft(draft.id);if(!row.jobBinding)throw Error('submission before durable binding');globalThis.commitCalls=(globalThis.commitCalls??0)+1;return [...body,1];}
  if(op===48){const b=decodeTrackJobBinding(new Uint8Array((await readTrackDraft(draft.id)).jobBinding));return [b.operationId,b.objectId,b.revisionHash,b.generation,b.revision,b.directory,b.directoryEpoch,b.fileHash,b.fileBytes,b.fileBytes,4,2,b.authorHash,0];}
  if(op!==82)throw Error('unexpected operation');
  const row=await readTrackDraft(draft.id),plan=decode(new Uint8Array(row.publicationPlan),4352),raw=plan[5],record=decode(raw,4096);
  if(globalThis.holdPreparation){globalThis.preparationWaiting=true;await new Promise(resolve=>{globalThis.resumeResponse=resolve;});globalThis.preparationWaiting=false;}
  return [body[0],1,await Identity.fullHash(join(domain('TrailMate.Track.ID.v1'),publicKey,record[2])),await Identity.fullHash(join(domain('TrailMate.Track.Revision.v1'),raw)),1];
 }};
 const root=createRoot(document.getElementById('root'));
 let fixtureLocale='zh-Hans',allowed=[];
 const directory={name:'Selected directory',capabilitiesVerified:true,protocol:'trailmate.track.v1',delivery:new Uint8Array(16).fill(4),epoch:new Uint8Array(16).fill(5)};
 const render=operations=>{allowed=operations;root.render(React.createElement(ManagementCopyProvider,{locale:fixtureLocale},React.createElement(TrackPreparation,{session,draft,directory,operations,acquire:()=>{if(locked)return false;locked=true;return {};},release:()=>{locked=false;}})));};
 globalThis.setTestLocale=locale=>{fixtureLocale=locale;render(allowed);};
 globalThis.enableFixture=()=>render([1,2,32,33,35,82]);render([]);
 globalThis.enablePublication=()=>render([1,2,32,33,35,82,83,48]);
 globalThis.switchDraft=async()=>{draft=await saveTrackDraft(source,{name:'第二条轨迹'});render([1,2,32,33,35,82]);};
};`;
const bundle=await build({stdin:{contents,resolveDir:process.cwd(),loader:'js'},nodePaths:[path.resolve('site/node_modules')],bundle:true,write:false,format:'iife',platform:'browser',outdir:'.codex-build/ui-check',loader:{'.svg':'dataurl','.png':'dataurl','.webp':'dataurl','.woff':'dataurl','.woff2':'dataurl','.ttf':'dataurl'},logLevel:'silent'});
const script=bundle.outputFiles.find(file=>file.path.endsWith('.js')).text;
const server=http.createServer((req,res)=>res.end('<!doctype html><div id="root"></div>'));await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
try{
 const page=await browser.newPage();await page.goto(`http://127.0.0.1:${server.address().port}`);await page.addScriptTag({content:script});await page.evaluate(()=>startFixture());
 const prepare=page.getByRole('button',{name:'准备轨迹签名',exact:true});await prepare.waitFor();assert.equal(await prepare.isDisabled(),true);
 await page.evaluate(()=>enableFixture());await prepare.click();await page.getByText('设备签名已准备，尚未发布到网络。',{exact:true}).waitFor();
 assert.match(await page.getByLabel('轨迹 ID',{exact:true}).inputValue(),/^[0-9a-f]{64}$/);assert.equal(await page.getByRole('button',{name:'复制轨迹 ID',exact:true}).count(),1);
 const objectId=await page.getByLabel('轨迹 ID',{exact:true}).inputValue();
 await page.context().grantPermissions(['clipboard-read','clipboard-write']);
 await page.getByRole('button',{name:'复制轨迹 ID',exact:true}).click();await page.getByText('轨迹 ID 已复制；此轨迹尚未发布到网络。',{exact:true}).waitFor();
 assert.equal(await page.evaluate(()=>navigator.clipboard.readText()),objectId);
 await page.evaluate(()=>{globalThis.holdPreparation=true;});await prepare.click();
 await page.waitForFunction(()=>globalThis.preparationWaiting===true);assert.equal(await prepare.isDisabled(),true);
 const waitingCalls=await page.evaluate(()=>serialCalls);
 for(const locale of locales){const text=trackPreparationText(locale.id);await page.evaluate(id=>setTestLocale(id),locale.id);await page.getByRole('button',{name:text.pause,exact:true}).waitFor();await page.getByText(text.checking,{exact:true}).waitFor();assert.equal(await page.evaluate(()=>serialCalls),waitingCalls);}
 await page.evaluate(()=>setTestLocale('zh-Hans'));
 await page.getByRole('button',{name:'暂停准备',exact:true}).click();
 await page.evaluate(()=>{globalThis.holdPreparation=false;globalThis.resumeResponse();});
 await page.getByText('准备已暂停；再次准备会复用保存的操作。',{exact:true}).waitFor();
 assert.equal(await page.getByLabel('轨迹 ID',{exact:true}).count(),0);
 await prepare.click();await page.getByText('设备签名已准备，尚未发布到网络。',{exact:true}).waitFor();
 assert.equal(await page.getByLabel('轨迹 ID',{exact:true}).inputValue(),objectId);
 await page.evaluate(()=>{globalThis.holdPreparation=true;});await prepare.click();await page.waitForFunction(()=>globalThis.preparationWaiting===true);
 await page.evaluate(()=>switchDraft());await page.waitForFunction(()=>!document.querySelector('[role="status"]'));
 await page.evaluate(()=>{globalThis.holdPreparation=false;globalThis.resumeResponse();});await page.waitForFunction(()=>!document.querySelector('button[disabled]'));
 assert.equal(await page.getByLabel('轨迹 ID',{exact:true}).count(),0);
 assert.equal(await page.getByText('设备签名已准备，尚未发布到网络。',{exact:true}).count(),0);
 await prepare.click();await page.getByText('设备签名已准备，尚未发布到网络。',{exact:true}).waitFor();
 assert.notEqual(await page.getByLabel('轨迹 ID',{exact:true}).inputValue(),objectId);
 const resultId=await page.getByLabel('轨迹 ID',{exact:true}).inputValue(),confirmedCalls=await page.evaluate(()=>serialCalls);
 for(const locale of locales){const text=trackPreparationText(locale.id);await page.evaluate(id=>setTestLocale(id),locale.id);await page.getByText(text.prepared,{exact:true}).waitFor();assert.equal(await page.getByLabel(text.id,{exact:true}).inputValue(),resultId);assert.equal(await page.getByLabel(text.id,{exact:true}).getAttribute('dir'),'ltr');assert.equal(await page.evaluate(()=>serialCalls),confirmedCalls);}
 await page.evaluate(()=>{setTestLocale('zh-Hans');holdAuthor=true;});await prepare.click();await page.waitForFunction(()=>authorWaiting);
 const authorCalls=await page.evaluate(()=>serialCalls);await page.getByRole('button',{name:'暂停准备',exact:true}).click();
 await page.evaluate(()=>{holdAuthor=false;resumeAuthor();});await page.getByText(trackPreparationText('zh-Hans').paused,{exact:true}).waitFor();
 assert.equal(await page.evaluate(()=>serialCalls),authorCalls);assert.equal(await page.getByLabel('轨迹 ID',{exact:true}).count(),0);
 await page.evaluate(()=>enablePublication());await prepare.click();await page.getByText('设备签名已准备，尚未发布到网络。',{exact:true}).waitFor();
 const publish=page.locator('.track-publish');await publish.getByRole('button',{name:'发布到所选目录',exact:true}).click();await publish.getByText(/目录已确认此版本/).waitFor();
 assert.equal(await page.evaluate(()=>commitCalls),1);assert.equal(await publish.getByRole('button',{name:'发布到所选目录',exact:true}).isDisabled(),true);
 const publishedCalls=await page.evaluate(()=>serialCalls);
 for(const locale of locales){await page.evaluate(id=>setTestLocale(id),locale.id);await publish.getByRole('heading',{name:trackPublishText(locale.id).title,exact:true}).waitFor();assert.equal(await publish.getByRole('button',{name:trackPublishText(locale.id).publish,exact:true}).isDisabled(),true);assert.equal(await page.evaluate(()=>serialCalls),publishedCalls);}
 console.log('Track preparation UI passed capability gating, real identity proof, clipboard, pause/resume and stale-response rejection after draft switch');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
