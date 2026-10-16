// Explicit local replica reader. Native test controls endpoint and reference.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {trackIPText} from '../../site/i18n/track-ip-labels.js';
import {trackConflictText} from '../../site/i18n/track-conflict-labels.js';
import {trackSharedText} from '../../site/i18n/track-shared-labels.js';
import {trackPackageText} from '../../site/i18n/track-package-labels.js';
import {trackVersionHistoryText} from '../../site/i18n/track-version-history-labels.js';
const [endpoint,seed,id,revision,fileHash,fileBytes,forkId]=process.argv.slice(2);
if(!/^ws:\/\/127\.0\.0\.1:\d+\/$/.test(endpoint)||!/^[a-f0-9]{32}$/.test(seed)||![id,revision,fileHash].every(value=>/^[a-f0-9]{64}$/.test(value)))throw Error('Explicit loopback replica parameters required');
const work=path.resolve('.codex-build/hostlink-codec/track-replication-network/browser');await fs.mkdir(work,{recursive:true});
await fs.writeFile(path.join(work,'NOTES.txt'),'Local native replica acceptance reader. Current browser track client, original signature/version history/exact KML. No publication/USB/RF/public network. Reusable bundle, cleanup after acceptance.');
const source=`import{TrackDirectoryClient}from'./tracks/src/protocol-client.js';import{Identity,toHex}from'@reticulum/core';import{trackShareLink,trackId}from'./tracks/src/shared-id.js';import{readSharedTrackPackage}from'./tracks/src/shared-track-package.js';import{readTrackConflict}from'./tracks/src/track-conflict.js';import{readTrackVersionHistory}from'./tracks/src/track-version-history.js';import{listTrackDrafts,readTrackDraft}from'./tracks/src/draft-store.js';import{verifyTrackManifest}from'./tracks/src/manifest.js';import{decode}from'./geocaching/src/protocol.js';
window.inspectSavedReplicaCopy=async()=>{const rows=await listTrackDrafts();if(rows.length!==1)throw Error('Expected one saved replica copy');const draft=await readTrackDraft(rows[0].id),proof=await verifyTrackManifest(decode(new Uint8Array(draft.sourceProof),8192));return{name:draft.name,fileHash:toHex(await Identity.fullHash(draft.source)),objectId:toHex(proof.objectId),revision:proof.record[4],title:proof.record[9],resources:await Promise.all(draft.attachmentRows.map(async row=>({id:row.id,hash:toHex(await Identity.fullHash(row.content))})))};};
window.readReplica=async({endpoint,seed,id,revision,fileHash,fileBytes,forkId})=>{
 const client=new TrackDirectoryClient();try{
  await client.connect(endpoint,null,[seed]);let directories=[];const until=Date.now()+20000;
  while(Date.now()<until){directories=await client.verifiedDirectories();if(directories.length)break;await new Promise(resolve=>setTimeout(resolve,100));}
  if(directories.length!==1)throw Error('Verified replica unavailable');
  const binary=text=>Uint8Array.from(text.match(/../g),part=>parseInt(part,16));
  const reference=hash=>trackShareLink(location.origin+'/',binary(id),{revision:binary(hash)});
  const entry=client.directories.get(directories[0].key);
  const bundle=await client.enqueue(()=>readSharedTrackPackage((op,body)=>client.request(entry,op,body),reference(revision),{packageSupported:entry.operations.includes(14)}));
  const result=bundle.detail;
  const history=await client.enqueue(()=>readTrackVersionHistory((op,body)=>client.request(entry,op,body),result));
  let fork=null;
  if(forkId){const evidence=await readTrackConflict((op,body)=>client.enqueue(()=>client.request(entry,op,body)),trackId(binary(forkId)));if(!evidence)throw Error('Replicated author fork missing');fork={objectId:toHex(evidence.objectId),revision:evidence.revision,signaturesVerified:evidence.authorSignaturesVerified,chainVerified:evidence.chainVerified,titles:evidence.versions.map(value=>value.record[9])};}
  return{directory:directories[0].delivery&&toHex(directories[0].delivery),current:result.current,state:result.record[6],revision:result.record[4],author:toHex(result.record[1]),fileHash:toHex(await Identity.fullHash(result.source)),fileBytes:result.source.length,history:history.versions.map(item=>item.record[4]),historyReferences:history.versions.map(item=>toHex(item.revisionHash)),historyChainVerified:history.complete,authorSignatureVerified:true,packageReady:bundle.ready,allComplete:bundle.allComplete,indexVerified:Boolean(bundle.attachmentIndex),resources:bundle.rows.map(row=>({id:row.id,verified:row.verified})),fork};
 }finally{await client.close();}
};`;
await build({stdin:{contents:source,loader:'js',resolveDir:path.resolve('site')},bundle:true,format:'esm',outfile:path.join(work,'page.js'),logLevel:'silent'});
const siteRoot=path.resolve('site');
const server=http.createServer(async(req,res)=>{
 try{
  const pathname=new URL(req.url,'http://localhost').pathname;
  if(pathname==='/page.js'){res.setHeader('Content-Type','text/javascript');res.end(await fs.readFile(path.join(work,'page.js')));return;}
  if(pathname==='/trail-mate/geocaching/network.json'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({endpoint,trackDiscoverySeeds:[seed]}));return;}
  if(pathname.startsWith('/trail-mate/')){
   const relative=decodeURIComponent(pathname.slice('/trail-mate/'.length)),file=path.resolve(siteRoot,relative.endsWith('/')?relative+'index.html':relative);
   if(!file.startsWith(siteRoot+path.sep)){res.writeHead(403).end();return;}
   const type={'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.svg':'image/svg+xml','.png':'image/png','.webp':'image/webp','.woff2':'font/woff2'}[path.extname(file)]??'application/octet-stream';
   res.setHeader('Content-Type',type);res.end(await fs.readFile(file));return;
  }
  res.setHeader('Content-Type','text/html');res.end('<!doctype html><script type="module" src="/page.js"></script>');
 }catch{res.writeHead(404).end();}
});
await new Promise(resolve=>server.listen(Number(process.env.TRAIL_MATE_REPLICA_PAGE_PORT??0),'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
try{
 const page=await browser.newPage(),errors=[];page.on('pageerror',error=>errors.push(String(error)));await page.goto(origin);await page.waitForFunction(()=>typeof window.readReplica==='function');
 const result=await page.evaluate(options=>window.readReplica(options),{endpoint,seed,id,revision,fileHash,fileBytes,forkId});
 assert.equal(result.fileHash,fileHash);assert.equal(result.fileBytes,Number(fileBytes));assert.equal(result.current,true);assert.equal(result.state,1);assert.equal(result.revision,3);assert.deepEqual(result.history,[3,2,1]);assert.deepEqual(errors,[]);
  assert.equal(result.packageReady,true);assert.equal(result.allComplete,true);assert.equal(result.indexVerified,true);assert.deepEqual(result.resources,[{id:'icon',verified:true}]);assert.equal(result.historyChainVerified,true);
 if(forkId){assert.equal(result.fork.objectId,forkId);assert.equal(result.fork.revision,2);assert.equal(result.fork.signaturesVerified,true);assert.equal(result.fork.chainVerified,false);assert.deepEqual([...result.fork.titles].sort(),['Fork left','Fork right']);}
  let productionPage=false,productionCopy=null,productionHistoryVerified=false,forkSelectedHistoryVerified=false;
  if(forkId){
   const ui=await browser.newPage({viewport:{width:1440,height:1000}}),uiErrors=[];ui.on('pageerror',error=>uiErrors.push(String(error)));
   // Map tiles are unrelated to protocol acceptance; avoid a public map request.
   await ui.route('https://*.tile.openstreetmap.org/**',route=>route.abort());
   await ui.goto(origin+'/trail-mate/tracks/?lang=zh-Hans');
   const ip=trackIPText('zh-Hans'),conflict=trackConflictText('zh-Hans'),shared=trackSharedText('zh-Hans'),pack=trackPackageText('zh-Hans'),historyText=trackVersionHistoryText('zh-Hans');
   await ui.waitForFunction(value=>document.querySelector('input[type=url]')?.value===value,endpoint);
   const directory=ui.getByLabel(ip.directory,{exact:true});await directory.waitFor();
   await ui.waitForFunction(label=>[...document.querySelectorAll('select')].find(node=>node.getAttribute('aria-label')===label)?.options.length===2,ip.directory);
   const key=await directory.locator('option').nth(1).getAttribute('value');assert.ok(key);await directory.selectOption(key);
   const binary=text=>Uint8Array.from(text.match(/../g),part=>parseInt(part,16));
   const {trackId}=await import('../../site/tracks/src/shared-id.js');
   await ui.getByLabel(shared.reference,{exact:true}).fill(trackId(binary(id)));
   await ui.getByRole('button',{name:shared.find,exact:true}).click();await ui.locator('.shared-track-detail').getByRole('heading',{name:'Native replica package',exact:true}).waitFor();
   const historyPanel=ui.locator('.track-shared-history');await historyPanel.getByRole('button',{name:historyText.read,exact:true}).click();await historyPanel.getByText(historyText.complete,{exact:true}).waitFor();assert.equal(await historyPanel.locator('li').count(),3);
   await ui.setViewportSize({width:1440,height:1400});await historyPanel.evaluate(element=>window.scrollTo(0,Math.max(0,element.getBoundingClientRect().top+scrollY-100)));
   await ui.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/track-version-history-production-page-zh-Hans.png'});
   await historyPanel.locator('li').nth(1).getByRole('button',{name:historyText.open,exact:true}).click();
   try{await ui.waitForFunction(hash=>document.querySelectorAll('.shared-track-detail > dl code')[1]?.textContent===hash,result.historyReferences[1]);}
   catch(error){await fs.writeFile(path.join(work,'history-open-failure.json'),JSON.stringify({expected:result.historyReferences[1],errors:uiErrors,details:await ui.locator('.shared-track-detail').allTextContents(),status:await ui.locator('[role=status],[role=alert]').allTextContents()},null,2));await ui.screenshot({path:path.join(work,'history-open-failure.png')});throw error;}
   assert.equal(await ui.locator('.track-maintenance').count(),0);productionHistoryVerified=true;
   await ui.getByLabel(shared.reference,{exact:true}).fill(trackId(binary(forkId)));
   await ui.getByRole('button',{name:shared.find,exact:true}).click();await ui.getByText(conflict.verified,{exact:true}).waitFor();
   assert.equal(await ui.locator('.shared-track-detail').count(),0);assert.equal(await ui.locator('#tracks-shared-detail .track-conflict-panel').count(),1);
   await ui.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/track-conflict-production-page-zh-Hans.png',fullPage:true});
   await ui.getByRole('button',{name:conflict.open,exact:true}).nth(0).click();await ui.locator('.shared-track-detail').getByRole('heading',{name:/Fork (left|right)/,level:4}).waitFor();
   await ui.getByText(conflict.historical,{exact:true}).waitFor();
   await historyPanel.getByRole('button',{name:historyText.read,exact:true}).click();await historyPanel.getByText(historyText.complete,{exact:true}).waitFor();assert.equal(await historyPanel.locator('li').count(),2);await ui.locator('.track-conflict-panel').getByText(historyText.branch,{exact:true}).waitFor();forkSelectedHistoryVerified=true;
   await ui.locator('.shared-track-package').getByRole('button',{name:pack.read,exact:true}).click();await ui.locator('.shared-track-resource').getByText(pack.verified,{exact:false}).waitFor();
   await ui.getByText(conflict.historical,{exact:true}).waitFor();assert.equal(await ui.locator('.track-maintenance').count(),0);
   await ui.setViewportSize({width:1440,height:1400});
   await ui.locator('.track-conflict-panel').evaluate(element=>window.scrollTo(0,Math.max(0,element.getBoundingClientRect().top+scrollY-100)));
   await ui.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/track-conflict-production-historical-zh-Hans.png'});
   const nativeDetail=await ui.locator('.shared-track-detail').getByRole('heading',{name:/Fork (left|right)/,level:4}).textContent();
   await ui.locator('.shared-track-detail').getByRole('button',{name:shared.saveCopy,exact:true}).click();
   await ui.locator('.tracks-row strong').filter({hasText:nativeDetail}).waitFor();
   assert.equal(await ui.locator('#tracks-shared-detail .track-conflict-panel').count(),1);
   await ui.getByRole('button',{name:ip.disconnect,exact:true}).click();await ui.getByText(ip.offline,{exact:true}).waitFor();
   await ui.locator('.track-conflict-panel').waitFor({state:'detached'});await ui.locator('.shared-track-detail').waitFor({state:'detached'});
   await ui.reload();await ui.locator('.tracks-row strong').filter({hasText:nativeDetail}).waitFor();
   const persisted=await ui.evaluate(async()=>{await import('/page.js');return window.inspectSavedReplicaCopy();});
   assert.equal(persisted.name,nativeDetail);assert.equal(persisted.title,nativeDetail);assert.equal(persisted.fileHash,fileHash);assert.equal(persisted.objectId,forkId);assert.equal(persisted.revision,2);assert.equal(persisted.resources.length,1);assert.equal(persisted.resources[0].id,'icon');
   assert.deepEqual(uiErrors,[]);productionPage=true;productionCopy=persisted;await ui.close();
  }
  await fs.writeFile(path.join(work,'result.json'),JSON.stringify({passed:true,...result,actualBrowser:true,productionPage,productionCopy,productionHistoryVerified,forkSelectedHistoryVerified,physicalUsb:false,publicNetwork:false},null,2));console.log('Fresh browser verified archived replica chain to root; production page read full history/pinned version, selected fork branch chain/package and persistent copy after source offline.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
