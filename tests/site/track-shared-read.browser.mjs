import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {FrameDecoder,encodeFrame,FRAME} from '../../site/device/src/hostlink.js';
import {decodeManagement,encodeManagement} from '../../site/device/src/management-protocol.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {encodeTrackManifest,trackManifestSigningBytes,verifyTrackManifest} from '../../site/tracks/src/manifest.js';
import {trackId} from '../../site/tracks/src/shared-id.js';
import {trackSharedText,trackSharedLabels,trackSharedKeys} from '../../site/i18n/track-shared-labels.js';
import {locales} from '../../site/i18n/registry.js';
import {trackBrowseText,trackBrowseKeys,trackBrowseLabels} from '../../site/i18n/track-browse-labels.js';
import {trackRegionText,trackRegionKeys,trackRegionLabels} from '../../site/i18n/track-region-labels.js';
import {trackFilterText} from '../../site/i18n/track-filter-labels.js';
import {trackOwnedText} from '../../site/i18n/track-owned-labels.js';
import {trackMaintenanceText} from '../../site/i18n/track-maintenance-labels.js';
import {kmlWorkspaceText} from '../../site/i18n/kml-workspace-labels.js';
import {routeInstallText} from '../../site/i18n/route-install-labels.js';
import {kmlResourceText} from '../../site/i18n/kml-resource-labels.js';
import {trackIPText} from '../../site/i18n/track-ip-labels.js';
import {kmlSimplifyText} from '../../site/i18n/kml-simplify-labels.js';
import {publicTrackLabels} from '../../site/i18n/public-track-labels.js';
import {installedRoutePath} from '../../site/tracks/src/device-route-install.js';
const author=await Identity.generate(),publicKey=await author.getPublicKey(),directory=await Identity.generate(),directoryPublic=await directory.getPublicKey();
async function destination(identity){return Identity.truncatedHash(Uint8Array.from([...(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),...identity.identityHash]));}
const delivery=await destination(author),directoryDelivery=await destination(directory),epoch=new Uint8Array(16).fill(8);
const source=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Document><name>Shared folder</name><Placemark id="line-1"><name>Shared mountain trail</name><description><![CDATA[<img src=x onerror="window.kmlExecuted=true">]]></description><TimeStamp><when>2026-10-12T00:00:00Z</when></TimeStamp><ExtendedData><Data name="survey"><displayName>Survey note</displayName><value>Original extension</value></Data></ExtendedData><LineString><coordinates>120,30 120.01,30.01</coordinates></LineString></Placemark>'+Array.from({length:25},(_,index)=>`<Placemark><name>${index===0?'x'.repeat(500):'Waypoint '+(index+1)}</name><Point><coordinates>120,30</coordinates></Point></Placemark>`).join('')+'<GroundOverlay id="shared-overlay"><name>Original overlay</name><Icon><href>https://invalid.example/'+('x'.repeat(500))+'.png</href></Icon></GroundOverlay></Document></kml>');
const fileHash=await Identity.fullHash(source),record=[1,publicKey,new Uint8Array(16).fill(7),0,1,null,0,100,100,'Shared mountain trail','Author description',[1200000000,300000000,1200100000,300100000],[0,null,null,27],[fileHash,source.length,'application/vnd.google-earth.kml+xml',1]];
const manifestRaw=encodeTrackManifest(record),signed=[manifestRaw,await author.sign(trackManifestSigningBytes(manifestRaw))],manifest=await verifyTrackManifest(signed),id=trackId(manifest.objectId);
const regionVersion=new Uint8Array(32).fill(4),regionHex=Buffer.from(regionVersion).toString('hex'),regionNames=['测试国家','测试省','测试市','测试区'];
const regionNodes=regionNames.map((name,index)=>[`test:r${index}`,index?`test:r${index-1}`:null,index,name,[['en',`Test level ${index}`]],[1190000000,290000000,1210000000,310000000],index===3?0:1]);
const boundaries=await Promise.all(regionNodes.map(async node=>{const raw=new TextEncoder().encode(JSON.stringify([1,regionHex,node[0],[[[[1190000000,290000000],[1210000000,290000000],[1210000000,310000000],[1190000000,310000000],[1190000000,290000000]]]]]));return {raw,hash:await Identity.fullHash(raw)};}));
const harness=path.resolve('.codex-build/hostlink-codec/shared-track-page-browser');await fs.mkdir(harness,{recursive:true});
await fs.writeFile(path.join(harness,'NOTES.txt'),'Current production tracks page with only the directory transport mocked for IP. Trail Mate web management goal; generated from current source; one reusable cache, remove after validation completes.');
const transportMock=`import {encode,decode} from '../../geocaching/src/protocol.js';
export class TrackDirectoryClient{
 constructor(notify){this.notify=notify;this.closed=false;this.generation=0;this.directories=new Map();}
 async connect(){const f=window.trackNetworkFixture,b=value=>Uint8Array.from(value);this.entry={key:'test',ready:true,identity:{getPublicKey:async()=>b(f.publicKey)},destination:b(f.delivery),epoch:b(f.epoch),name:'Test directory'};this.directories.set('test',this.entry);this.notify({type:'sources'});}
 async verifiedDirectories(){return this.entry?[{key:'test',name:'Test directory',capabilitiesVerified:true,publicKey:await this.entry.identity.getPublicKey(),delivery:this.entry.destination,epoch:this.entry.epoch}]:[];}
 async enqueue(callback){return callback();}
 async request(entry,operation,body){if(this.closed||entry!==this.entry)throw Error('Directory changed');return decode(Uint8Array.from(await window.ipDirectoryReply(operation,[...encode(body)])),8192);}
 async close(){this.closed=true;this.generation++;}
}`;
await build({entryPoints:[path.resolve('site/tracks/src/page.jsx')],bundle:true,format:'esm',outfile:path.join(harness,'tracks.js'),loader:{'.png':'dataurl','.webp':'dataurl','.svg':'dataurl','.woff2':'dataurl','.woff':'dataurl','.ttf':'dataurl'},plugins:[{name:'directory-transport-fixture',setup(builder){builder.onLoad({filter:/tracks[\\/]src[\\/]protocol-client\.js$/},args=>({contents:transportMock,loader:'js',resolveDir:path.dirname(args.path)}));}}],logLevel:'silent'});
const root=path.resolve('site'),types={'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2'};
const server=http.createServer(async(req,res)=>{try{let url=decodeURIComponent(new URL(req.url,'http://localhost').pathname);if(url.endsWith('/'))url+='index.html';const fixture=url.match(/^\/tracks\/dist\/(tracks\.(?:js|css))$/),filename=fixture?path.join(harness,fixture[1]):path.resolve(root,'.'+url);if(!fixture&&!filename.startsWith(root+path.sep))throw Error('path');res.setHeader('Content-Type',types[path.extname(filename)]??'application/octet-stream');res.end(await fs.readFile(filename));}catch{res.writeHead(404).end();}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser,current,managementCalls=0;const beginCalls=[],queryBoxes=[],queryFilters=[];
let routeUpload,routeInstallCalls=0,routeQueryCalls=0,rejectAuthor=false,holdRead=false,releaseRead,connectedAuthor=author;
try{
 browser=await chromium.launch({headless:true,channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL||undefined});
 const page=await browser.newPage({viewport:{width:1440,height:1000}}),errors=[];page.setDefaultTimeout(10000);page.on('pageerror',error=>errors.push(error.message));
 const resourceRequests=[];page.on('request',request=>{if(request.url().startsWith('https://invalid.example/'))resourceRequests.push(request.url());});
 await page.exposeFunction('sharedReply',async input=>{
  const frame=new FrameDecoder().push(Uint8Array.from(input))[0];assert.ok(frame);
  if(frame.type===FRAME.HELLO)return [...encodeFrame(FRAME.HELLO_ACK,frame.sequence,new Uint8Array([1,0,0,2,0,4,0,0,6,...new TextEncoder().encode('Wio L2'),4,...new TextEncoder().encode('test')]))];
  const request=decodeManagement(frame.payload);managementCalls++;let body;
  assert.ok(![89,90,91,92,93,94].includes(request.code),'Browser network must not use device RF directory commands');
  if(request.code===1&&rejectAuthor)return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,4,[]))];
  if(request.code===0)body=[1,512,320,1,[0,1,2,32,33,34,35,36,80,85,95],7];
  else if(request.code===32){assert.equal(request.body[1],3);routeUpload={id:request.body[0],total:request.body[2],hash:request.body[3],bytes:[]};body=[routeUpload.id,320,0,0];}
  else if(request.code===33){assert.deepEqual(request.body[0],routeUpload.id);assert.equal(request.body[1],routeUpload.bytes.length);routeUpload.bytes.push(...request.body[2]);body=[routeUpload.id,320,routeUpload.bytes.length,0];}
  else if(request.code===35){assert.equal(routeUpload.bytes.length,routeUpload.total);assert.deepEqual(await Identity.fullHash(Uint8Array.from(routeUpload.bytes)),routeUpload.hash);body=[routeUpload.id,320,routeUpload.total,1];}
  else if(request.code===85){routeInstallCalls++;assert.deepEqual(request.body,[routeUpload.id,routeUpload.hash,routeUpload.total,9]);body=[routeUpload.id,routeUpload.hash,routeUpload.total,installedRoutePath(routeUpload.hash),0];}
  else if(request.code===95){routeQueryCalls++;assert.deepEqual(request.body,[routeUpload.id,routeUpload.hash,routeUpload.total,9]);body=[routeUpload.id,routeUpload.hash,routeUpload.total,installedRoutePath(routeUpload.hash),routeQueryCalls===1?0:1];}
  else if(request.code===80)body=[1,null,null];
  else if(request.code===89)body=[null,[[directoryDelivery,directoryPublic,epoch,'Test directory',1,8388608,1024,32]]];
  else if(request.code===1)body=[await connectedAuthor.getPublicKey(),connectedAuthor.identityHash,await destination(connectedAuthor),9];
  else if(request.code===2){const connectedPublic=await connectedAuthor.getPublicKey();body=[connectedPublic,connectedAuthor.identityHash,await destination(connectedAuthor),9,await connectedAuthor.sign(authorProofMessage(request.session,request.body[0],connectedPublic,9))];}
  else if(request.code===91){const [networkId,dest,version,generation,op,parameters]=request.body;assert.deepEqual(dest,directoryDelivery);assert.deepEqual(version,epoch);assert.equal(generation,9);beginCalls.push(op);
   let result;
   if(op===1){const box=parameters[0];queryBoxes.push(box);queryFilters.push({author:parameters[2],state:parameters[3]});const [west,south,east,north]=record[11],intersects=south<=box[3]&&box[1]<=north&&west<=box[2]&&box[0]<=east;
    const summary=[manifest.objectId,manifest.revisionHash,1,0,0,record[9],record[11],record[12],source.length,publicKey];
    if(box[0]===-1800000000&&box[2]===1799999999&&parameters[2]){
     const binding=await Identity.fullHash(encode([box,parameters[1],parameters[2],parameters[3]]));
     if(parameters[6]){assert.equal(parameters[5],1);assert.deepEqual(parameters[6],[manifest.objectId,binding]);result=[1,null,[[new Uint8Array(32).fill(255),new Uint8Array(32).fill(254),1,0,0,'Second owned trail',record[11],record[12],source.length,publicKey]]];}
     else result=[1,[manifest.objectId,binding],[summary]];
    }else result=[1,null,intersects?[summary]:[]];
   }else if(op===9)result=[1,'trailmate.track.regions.v1','test',regionVersion,3,32,1024,[9,10,11,12,13]];
   else if(op===10){assert.equal(parameters[0],'test');assert.deepEqual(parameters[1],regionVersion);result=[regionVersion,null,regionNodes.filter(node=>node[1]===parameters[2])];}
   else if(op===11){const index=regionNodes.findIndex(node=>node[0]===parameters[1]);assert.ok(index>=0);result=[regionVersion,regionNodes[index],['region-e7-json-v1',boundaries[index].hash,boundaries[index].raw.length]];}
   else if(op===12){const index=regionNodes.findIndex(node=>node[0]===parameters[1]),boundary=boundaries[index];assert.deepEqual(parameters[3],boundary.hash);result=[boundary.hash,boundary.raw.length,parameters[5],boundary.raw.slice(parameters[5],parameters[5]+parameters[6])];}
   else if(op===13){assert.equal(parameters[1],'test:r3');assert.deepEqual(parameters[3],regionNodes[3][5]);assert.deepEqual(parameters[5],publicKey);assert.equal(parameters[6],null);result=[9007199254740997n,null,[[manifest.objectId,manifest.revisionHash,1,0,0,record[9],record[11],record[12],source.length,publicKey]]];}
   else result=op===2?[signed,1]:[fileHash,source.length,parameters[2],source.slice(parameters[2],parameters[2]+parameters[3])];
   const raw=encode([1,1,op,networkId,200,result]);current={id:networkId,op,raw,hash:await Identity.fullHash(raw)};body=[networkId,op];
  }else if(request.code===92)body=[current.id,current.op,200,current.raw.length,current.hash];
  else if(request.code===93)body=[current.hash,current.raw.length,request.body[1],current.raw.slice(request.body[1],request.body[1]+request.body[2])];
  else if(request.code===94){current=undefined;body=[];}
  else assert.fail(`Unexpected command ${request.code}`);
  return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,0,body))];
 });
 await page.exposeFunction('sharedQueryCount',()=>queryBoxes.length);
 await page.exposeFunction('sharedReadPending',()=>Boolean(releaseRead));
 await page.exposeFunction('ipDirectoryReply',async(op,encoded)=>{
  const parameters=decode(Uint8Array.from(encoded)),summary=[manifest.objectId,manifest.revisionHash,1,0,0,record[9],record[11],record[12],source.length,publicKey];beginCalls.push(op);let result;
  if(op===1){const box=parameters[0];queryBoxes.push(box);queryFilters.push({author:parameters[2],state:parameters[3]});const [west,south,east,north]=record[11],intersects=south<=box[3]&&box[1]<=north&&west<=box[2]&&box[0]<=east;
   if(box[0]===-1800000000&&box[2]===1799999999&&parameters[2]){
    const binding=await Identity.fullHash(encode([box,parameters[1],parameters[2],parameters[3]]));
    if(parameters[6]){assert.equal(parameters[5],1);assert.deepEqual(parameters[6],[manifest.objectId,binding]);result=[1,null,[[new Uint8Array(32).fill(255),new Uint8Array(32).fill(254),1,0,0,'Second owned trail',record[11],record[12],source.length,publicKey]]];}
    else result=[1,[manifest.objectId,binding],[summary]];
   }else result=[1,null,intersects?[summary]:[]];
  }else if(op===2){if(holdRead)await new Promise(resolve=>{releaseRead=resolve;});result=[signed,1];}
  else if(op===8)result=[fileHash,source.length,parameters[1],source.slice(parameters[1],parameters[1]+parameters[2])];
  else if(op===9)result=[1,'trailmate.track.regions.v1','test',regionVersion,3,32,1024,[9,10,11,12,13]];
  else if(op===10){assert.deepEqual(parameters[0],regionVersion);result=[regionVersion,null,regionNodes.filter(node=>node[1]===parameters[1])];}
  else if(op===11){const index=regionNodes.findIndex(node=>node[0]===parameters[0]);assert.ok(index>=0);assert.deepEqual(parameters[1],regionVersion);result=[regionVersion,regionNodes[index],['region-e7-json-v1',boundaries[index].hash,boundaries[index].raw.length]];}
  else if(op===12){const index=regionNodes.findIndex(node=>node[0]===parameters[0]),boundary=boundaries[index];assert.ok(index>=0);assert.deepEqual(parameters[1],regionVersion);result=[boundary.hash,boundary.raw.length,parameters[2],boundary.raw.slice(parameters[2],parameters[2]+parameters[3])];}
  else if(op===13){assert.equal(parameters[0],'test:r3');assert.deepEqual(parameters[1],regionVersion);assert.ok(parameters[4]===null||Buffer.from(parameters[4]).equals(Buffer.from(publicKey)));assert.ok(parameters[5]===null||parameters[5]===0);result=[9007199254740997n,null,[summary]];}
  else assert.fail('Unexpected IP operation '+op);
  return [...encode(result)];
 });
 await page.addInitScript(data=>{window.trackNetworkFixture=data;},{publicKey:[...directoryPublic],delivery:[...directoryDelivery],epoch:[...epoch]});
 await page.addInitScript(()=>{
  let reader;const port={async open(){this.readable=new ReadableStream({start(controller){reader=controller;}});this.writable=new WritableStream({async write(bytes){reader.enqueue(Uint8Array.from(await window.sharedReply([...bytes])));}});},async setSignals(){},async close(){},getInfo(){return {usbVendorId:0x2886,usbProductId:0x802f};}};
  Object.defineProperty(navigator,'serial',{configurable:true,value:{async requestPort(){return port;}}});
  Object.defineProperty(navigator,'clipboard',{configurable:true,value:{async writeText(value){window.copiedTrack=value;}}});
 });
 await page.goto(`http://127.0.0.1:${server.address().port}/tracks/?lang=zh-Hans&id=${id}`);
 await page.evaluate(()=>{window.mockSerial=navigator.serial;Object.defineProperty(navigator,'serial',{configurable:true,value:undefined});});
 const ipText=trackIPText('zh-Hans');await page.getByLabel(ipText.endpoint,{exact:true}).fill('wss://test.invalid/');await page.getByRole('button',{name:ipText.connect,exact:true}).click();
 await page.getByLabel(ipText.directory,{exact:true}).selectOption('test');
 const endpointStyle=await page.getByLabel(ipText.endpoint,{exact:true}).evaluate(element=>({box:getComputedStyle(element).boxSizing,radius:getComputedStyle(element).borderRadius,width:element.getBoundingClientRect().width,parent:element.parentElement.getBoundingClientRect().width}));
 assert.equal(endpointStyle.box,'border-box');assert.equal(endpointStyle.radius,'12px');assert.ok(Math.abs(endpointStyle.width-endpointStyle.parent)<1);
 const panel=page.locator('.track-shared-read');
 assert.match(await panel.getByLabel('轨迹 ID 或分享链接',{exact:true}).inputValue(),/id=tmtrk1_/);
 const browse=page.locator('.shared-track-browser');
 assert.equal(managementCalls,0);assert.equal(await browse.getByLabel('仅我的共享轨迹',{exact:true}).isDisabled(),true);assert.equal(await browse.getByRole('button',{name:'查看我的全部共享轨迹',exact:true}).isDisabled(),true);
 await browse.getByRole('button',{name:'搜索当前地图范围',exact:true}).click();await browse.locator('.shared-track-results strong').getByText('Shared mountain trail',{exact:true}).waitFor();
 const beforeMove=queryBoxes.length;
 await page.evaluate(()=>{scrollTo(0,0);document.querySelector('.sidebar').scrollTop=0;});
 await page.screenshot({path:'.codex-build/hostlink-codec/shared-track-map-browse.png'});
 await browse.getByLabel('移动地图后自动搜索',{exact:true}).check();
 const box=await page.locator('.shared-track-browse-map').boundingBox();
 await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.move(box.x+box.width/2+70,box.y+box.height/2,{steps:5});await page.mouse.up();
 await page.waitForFunction(async count=>await window.sharedQueryCount()>count,beforeMove);
 await browse.getByRole('button',{name:'搜索当前地图范围',exact:true}).waitFor({state:'visible'});
 await browse.getByLabel('移动地图后自动搜索',{exact:true}).uncheck();
 await browse.getByRole('button',{name:'打开轨迹',exact:true}).click();await panel.getByText('作者签名验证通过。',{exact:true}).waitFor();
 const detail=page.locator('.shared-track-detail');
 await detail.getByRole('button',{name:'复制 ID',exact:true}).click();assert.equal(await page.evaluate(()=>window.copiedTrack),id);assert.equal(await detail.locator('.track-maintenance').count(),0);assert.equal(managementCalls,0);
 await detail.getByRole('heading',{name:'Shared mountain trail',exact:true}).waitFor();assert.deepEqual(beginCalls.filter(op=>op!==1),[2]);
 const calls=managementCalls,reference=await panel.getByLabel('轨迹 ID 或分享链接',{exact:true}).inputValue();
 for(const locale of locales){assert.equal(publicTrackLabels[locale.id].length,3);assert.equal(trackSharedLabels[locale.id].length,trackSharedKeys.length);assert.equal(trackBrowseLabels[locale.id].length,trackBrowseKeys.length);const text=trackSharedText(locale.id),mapText=trackBrowseText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await panel.getByText(publicTrackLabels[locale.id][0],{exact:true}).waitFor();await panel.getByRole('heading',{name:text.title,exact:true}).waitFor();await browse.getByRole('heading',{name:mapText.title,exact:true}).waitFor();assert.equal(await panel.getByLabel(text.reference,{exact:true}).inputValue(),reference);assert.equal(await panel.getByLabel(text.reference,{exact:true}).getAttribute('dir'),'ltr');await detail.getByRole('heading',{name:'Shared mountain trail',exact:true}).waitFor();assert.equal(managementCalls,calls);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 assert.equal(await detail.locator('.shared-track-statistics').count(),0);
 await detail.getByRole('button',{name:'下载已验证的 KML',exact:true}).click();await detail.getByRole('link',{name:'下载已验证的 KML',exact:true}).waitFor();assert.deepEqual(beginCalls.filter(op=>op!==1),[2,2,...Array(Math.ceil(source.length/1024)).fill(8)]);
 const statistics=detail.locator('.shared-track-statistics');await statistics.waitFor();const callsBeforeStatistics=managementCalls;
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);const labels=kmlWorkspaceText(locale.id);
  const formatted=await page.evaluate(locale=>{const number=new Intl.NumberFormat(locale);return {two:number.format(27),one:number.format(1),distance:number.format(1.47)};},locale.id);
  assert.equal(await statistics.locator('span').count(),4);
  await statistics.getByText(labels.points.replace('{count}',formatted.two).replace('{segments}',formatted.one),{exact:true}).waitFor();
  await statistics.getByText(labels.distance.replace('{value}',formatted.distance+' km'),{exact:true}).waitFor();
  await statistics.getByText(labels.ascent.replace('{value}',labels.unknown),{exact:true}).waitFor();
  await statistics.getByText(labels.duration.replace('{value}',labels.unknown),{exact:true}).waitFor();
  assert.equal(managementCalls,callsBeforeStatistics);
 }
 await page.locator('.management-language select').selectOption('zh-Hans');
 const contents=detail.locator('.shared-kml-contents');await contents.locator(':scope > summary').click();
 assert.equal(await contents.locator('.shared-kml-feature').count(),25);
 await contents.getByText('<img src=x onerror="window.kmlExecuted=true">',{exact:true}).waitFor();assert.equal(await contents.locator('img').count(),0);assert.equal(await page.evaluate(()=>window.kmlExecuted),undefined);
 await contents.getByText('ID: line-1',{exact:true}).waitFor();await contents.getByText('Shared folder',{exact:true}).first().waitFor();
 await contents.locator('.shared-kml-extensions summary').click();await contents.getByText('Survey note',{exact:true}).waitFor();await contents.getByText('Original extension',{exact:true}).waitFor();
 await contents.getByRole('button',{name:'下一页',exact:true}).click();assert.equal(await contents.locator('.shared-kml-feature').count(),1);await contents.getByRole('heading',{name:'Waypoint 25',exact:true}).waitFor();
 await contents.getByRole('button',{name:'上一页',exact:true}).click();assert.equal(await contents.locator('.shared-kml-feature').count(),25);
 const resources=contents.locator('.shared-kml-resources');await resources.locator(':scope > summary').click();
 await resources.getByText('ID: shared-overlay',{exact:true}).waitFor();assert.equal(await resources.locator('.shared-kml-resource').count(),1);
 assert.equal(await resources.locator('a,img,iframe').count(),0);
 const resourceCommands=managementCalls;
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);await resources.getByText(kmlResourceText(locale.id).notice,{exact:true}).waitFor();
  assert.equal(managementCalls,resourceCommands);await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
 }
 await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:1000});assert.deepEqual(resourceRequests,[]);
 await page.setViewportSize({width:390,height:844});assert.equal(await contents.getByRole('heading',{name:'x'.repeat(500),exact:true}).textContent(),'x'.repeat(500));assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);await page.setViewportSize({width:1400,height:1000});
 const href=await detail.getByRole('link',{name:'下载已验证的 KML',exact:true}).getAttribute('href');assert.deepEqual(await page.evaluate(async value=>[...new Uint8Array(await (await fetch(value)).arrayBuffer())],href),[...source]);
 const regions=page.locator('.shared-track-regions');
 await regions.getByRole('button',{name:'加载行政区',exact:true}).click();
 await regions.getByLabel('选择行政区',{exact:true}).selectOption('test:r0');
 for(let level=1;level<4;level++){await regions.getByLabel('选择行政区',{exact:true}).selectOption(`test:r${level}`);}
 await browse.getByRole('button',{name:'搜索所选行政区',exact:true}).click();await browse.locator('.shared-track-results strong').getByText('Shared mountain trail',{exact:true}).waitFor();assert.equal(managementCalls,0);
 await browse.evaluate(element=>{window.publicBrowseElement=element;});
 await page.evaluate(()=>Object.defineProperty(navigator,'serial',{configurable:true,value:window.mockSerial}));
 await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await panel.getByText(publicTrackLabels['zh-Hans'][1],{exact:true}).waitFor();
 assert.equal(await browse.evaluate(element=>element===window.publicBrowseElement),true);assert.equal(await regions.locator('nav button').count(),4);await detail.getByRole('link',{name:'下载已验证的 KML',exact:true}).waitFor();assert.equal(await panel.getByLabel('轨迹 ID 或分享链接',{exact:true}).inputValue(),reference);
 const verifiedHref=await detail.getByRole('link',{name:'下载已验证的 KML',exact:true}).getAttribute('href');
 await page.getByRole('button',{name:'断开设备',exact:true}).click();await panel.getByText(publicTrackLabels['zh-Hans'][0],{exact:true}).waitFor();
 assert.equal(await detail.getByRole('link',{name:'下载已验证的 KML',exact:true}).getAttribute('href'),verifiedHref);assert.equal(await browse.evaluate(element=>element===window.publicBrowseElement),true);assert.equal(await regions.locator('nav button').count(),4);assert.equal(await detail.locator('.track-maintenance').count(),0);assert.equal(await browse.getByLabel('仅我的共享轨迹',{exact:true}).isDisabled(),true);
 const disconnectedCalls=managementCalls;await browse.getByRole('button',{name:'搜索所选行政区',exact:true}).click();await browse.locator('.shared-track-results strong').getByText('Shared mountain trail',{exact:true}).waitFor();assert.equal(managementCalls,disconnectedCalls);
 await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await panel.getByText(publicTrackLabels['zh-Hans'][1],{exact:true}).waitFor();
 holdRead=true;await panel.getByRole('button',{name:'查看轨迹',exact:true}).click();await page.waitForFunction(async()=>window.sharedReadPending());await page.getByRole('button',{name:'断开设备',exact:true}).click();const inFlightCalls=managementCalls;holdRead=false;releaseRead();releaseRead=undefined;
 await panel.getByText('作者签名验证通过。',{exact:true}).waitFor();await detail.getByRole('heading',{name:'Shared mountain trail',exact:true}).waitFor();assert.equal(managementCalls,inFlightCalls);assert.equal(await browse.evaluate(element=>element===window.publicBrowseElement),true);
 await detail.getByRole('button',{name:'下载已验证的 KML',exact:true}).click();await detail.getByRole('link',{name:'下载已验证的 KML',exact:true}).waitFor();assert.equal(managementCalls,inFlightCalls);
 await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await panel.getByText(publicTrackLabels['zh-Hans'][1],{exact:true}).waitFor();
 await browse.getByLabel('仅我的共享轨迹',{exact:true}).check();await browse.getByLabel('包含已归档轨迹',{exact:true}).check();
 await browse.getByRole('button',{name:'搜索所选行政区',exact:true}).click();await browse.locator('.shared-track-results strong').getByText('Shared mountain trail',{exact:true}).waitFor();
 assert.ok(beginCalls.includes(13));assert.equal(await browse.getByLabel('移动地图后自动搜索',{exact:true}).isDisabled(),true);
 assert.equal(await regions.locator('nav button').count(),4);
 const regionCalls=managementCalls;
 for(const locale of locales){assert.equal(trackRegionLabels[locale.id].length,trackRegionKeys.length);await page.locator('.management-language select').selectOption(locale.id);await regions.getByRole('heading',{name:trackRegionText(locale.id).title,exact:true}).waitFor();await browse.getByRole('button',{name:trackRegionText(locale.id).search,exact:true}).waitFor();assert.equal(await browse.getByLabel(trackFilterText(locale.id).mine,{exact:true}).isChecked(),true);assert.equal(await browse.getByLabel(trackFilterText(locale.id).archived,{exact:true}).isChecked(),true);assert.equal(await regions.locator('nav button').count(),4);assert.equal(managementCalls,regionCalls);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.evaluate(()=>{scrollTo(0,0);document.querySelector('.sidebar').scrollTop=0;});
 await page.screenshot({path:'.codex-build/hostlink-codec/shared-track-region-search.png',fullPage:true});
 await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);await page.setViewportSize({width:1440,height:1000});
 await regions.getByRole('button',{name:'改用地图范围',exact:true}).click();await browse.getByRole('button',{name:'搜索当前地图范围',exact:true}).waitFor();assert.equal(await browse.getByLabel('移动地图后自动搜索',{exact:true}).isDisabled(),false);
 await browse.getByRole('button',{name:'搜索当前地图范围',exact:true}).click();await browse.locator('.shared-track-results strong').getByText('Shared mountain trail',{exact:true}).waitFor();assert.deepEqual(queryFilters.at(-1).author,publicKey);assert.equal(queryFilters.at(-1).state,null);
 await browse.getByRole('button',{name:'查看我的全部共享轨迹',exact:true}).click();await browse.getByText(/我在此目录中的共享轨迹/).waitFor();assert.deepEqual(queryBoxes.at(-1),[-1800000000,-900000000,1799999999,900000000]);assert.deepEqual(queryFilters.at(-1).author,publicKey);assert.equal(await browse.getByLabel('移动地图后自动搜索',{exact:true}).isChecked(),false);
 const ownedCalls=managementCalls;for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await browse.getByRole('button',{name:trackOwnedText(locale.id).all,exact:true}).waitFor();await browse.getByText(trackOwnedText(locale.id).results,{exact:false}).waitFor();assert.equal(managementCalls,ownedCalls);}await page.locator('.management-language select').selectOption('zh-Hans');
 const ownerBeforePan=await browse.locator('.shared-track-results code').allTextContents(),ownerQueryCount=queryBoxes.length,ownerMap=await browse.locator('.shared-track-browse-map').boundingBox();await page.mouse.move(ownerMap.x+ownerMap.width/2,ownerMap.y+ownerMap.height/2);await page.mouse.down();await page.mouse.move(ownerMap.x+ownerMap.width/2+60,ownerMap.y+ownerMap.height/2,{steps:5});await page.mouse.up();await page.waitForTimeout(1000);assert.deepEqual(await browse.locator('.shared-track-results code').allTextContents(),ownerBeforePan);assert.equal(queryBoxes.length,ownerQueryCount);await browse.getByRole('button',{name:'下一页',exact:true}).waitFor();
 await browse.getByRole('button',{name:'下一页',exact:true}).click();await browse.locator('.shared-track-results strong').getByText('Second owned trail',{exact:true}).waitFor();assert.equal(await browse.getByRole('button',{name:'下一页',exact:true}).count(),0);assert.equal(await browse.locator('.shared-track-results strong').getByText('Shared mountain trail',{exact:true}).count(),0);
 await page.getByRole('button',{name:'断开设备',exact:true}).click();await panel.getByText(publicTrackLabels['zh-Hans'][0],{exact:true}).waitFor();await page.waitForFunction(()=>!document.querySelector('.shared-track-browser input[type=checkbox]').checked);assert.equal(await browse.getByLabel('仅我的共享轨迹',{exact:true}).isChecked(),false);assert.equal(await browse.locator('.shared-track-results li').count(),0);assert.equal(await regions.locator('nav button').count(),4);
 connectedAuthor=await Identity.generate();await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await panel.getByText(publicTrackLabels['zh-Hans'][1],{exact:true}).waitFor();assert.equal(await detail.locator('.track-maintenance').count(),0);await detail.getByRole('link',{name:'下载已验证的 KML',exact:true}).waitFor();assert.equal(await browse.evaluate(element=>element===window.publicBrowseElement),true);
 await page.getByRole('button',{name:'断开设备',exact:true}).click();connectedAuthor=author;await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await panel.getByText(publicTrackLabels['zh-Hans'][1],{exact:true}).waitFor();await detail.getByRole('button',{name:'创建归档草稿',exact:true}).waitFor();
 const revised=Buffer.from(new TextDecoder().decode(source).replace('120.01,30.01','120.02,30.02'));
 await detail.getByLabel('使用修订 KML 更新',{exact:true}).setInputFiles({name:'revised.kml',mimeType:'application/vnd.google-earth.kml+xml',buffer:revised});await detail.getByText('后续版本草稿已保存，请准备设备签名后发布。',{exact:true}).waitFor();
 await detail.getByRole('button',{name:'创建归档草稿',exact:true}).click();await detail.getByRole('button',{name:'创建归档草稿',exact:true}).isEnabled();await page.waitForFunction(()=>!document.querySelector('.track-shared-read button')?.disabled);
 const plans=await page.evaluate(async()=>{const db=await new Promise((resolve,reject)=>{const r=indexedDB.open('trailmate-track-drafts');r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);});try{return await new Promise((resolve,reject)=>{const r=db.transaction('metadata').objectStore('metadata').getAll();r.onsuccess=()=>resolve(r.result.filter(row=>row.publicationPlan).map(row=>[...new Uint8Array(row.publicationPlan)]));r.onerror=()=>reject(r.error);});}finally{db.close();}});
 assert.equal(plans.length,2);const successorRows=plans.map(raw=>decode(decode(Uint8Array.from(raw))[5]));assert.deepEqual(successorRows.map(row=>row[6]).sort(),[0,1]);for(const row of successorRows){assert.equal(row[4],2);assert.deepEqual(row[2],record[2]);assert.deepEqual(row[5],manifest.revisionHash);}assert.deepEqual(successorRows.find(row=>row[6]===1)[13],record[13]);
 const maintenanceCalls=managementCalls;for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await detail.getByRole('button',{name:trackMaintenanceText(locale.id).update,exact:true}).waitFor();await detail.getByRole('button',{name:trackMaintenanceText(locale.id).archive,exact:true}).waitFor();assert.equal(managementCalls,maintenanceCalls);}await page.locator('.management-language select').selectOption('zh-Hans');
 await detail.getByRole('button',{name:'打开后续版本草稿',exact:true}).click();await page.locator('.tracks-editor').waitFor();await page.waitForFunction(()=>document.querySelectorAll('.tracks-row').length===2);assert.equal(await page.locator('.tracks-row').count(),2);
 await page.locator('.tracks-row').nth(1).click();const workspace=kmlWorkspaceText('zh-Hans');await page.locator('.tracks-editor').getByLabel(workspace.title,{exact:true}).fill('Edited owned update');await page.getByRole('button',{name:workspace.save,exact:true}).click();await page.locator('.tracks-row strong').getByText('Edited owned update',{exact:true}).waitFor();
 const editedOwned=await page.evaluate(async()=>{const db=await new Promise(resolve=>{const r=indexedDB.open('trailmate-track-drafts');r.onsuccess=()=>resolve(r.result);});try{return await new Promise(resolve=>{const r=db.transaction('metadata').objectStore('metadata').getAll();r.onsuccess=()=>{const row=r.result.find(row=>row.name==='Edited owned update');resolve({version:row.version,predecessor:!!row.predecessor,plan:!!row.publicationPlan,archive:row.archive});};});}finally{db.close();}});assert.deepEqual(editedOwned,{version:2,predecessor:true,plan:false,archive:false});
 await page.evaluate(()=>{scrollTo(0,0);document.querySelector('.sidebar').scrollTop=0;});
 await page.screenshot({path:'.codex-build/hostlink-codec/shared-track-detail.png',fullPage:true});
 await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
 await detail.getByRole('button',{name:'保存可编辑副本',exact:true}).click();await page.waitForFunction(()=>document.querySelectorAll('.tracks-row').length===3);
 assert.equal(await page.locator('.tracks-row').count(),3);
 const ownInstall=page.locator('.own-route-install .route-install-panel');
 assert.equal(await ownInstall.getByRole('button',{name:routeInstallText('zh-Hans').install,exact:true}).isDisabled(),false);
 const previewCalls=managementCalls,previewText=kmlSimplifyText('zh-Hans'),previewPanel=page.locator('.tracks-simplify');await previewPanel.getByRole('button',{name:previewText.preview,exact:true}).click();await previewPanel.getByText(previewText.pending,{exact:true}).waitFor();assert.equal(await ownInstall.getByRole('button',{name:routeInstallText('zh-Hans').install,exact:true}).isDisabled(),true);assert.equal(managementCalls,previewCalls);await previewPanel.getByRole('button',{name:previewText.cancel,exact:true}).click();await page.waitForFunction(()=>!document.querySelector('.own-route-install .route-install-panel button')?.disabled);assert.equal(managementCalls,previewCalls);
 const routePanel=page.locator('#tracks-shared-detail .route-install-panel');
 await routePanel.getByRole('button',{name:routeInstallText('zh-Hans').restore,exact:true}).click();
 await routePanel.getByRole('status').filter({hasText:routeInstallText('zh-Hans').noRecord}).waitFor();
 const emptyCommands=managementCalls;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await routePanel.getByRole('status').filter({hasText:routeInstallText(locale.id).noRecord}).waitFor();assert.equal(managementCalls,emptyCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');assert.equal(routeInstallCalls,0);assert.equal(routeQueryCalls,0);
 page.once('dialog',dialog=>dialog.accept());
 await routePanel.getByRole('button',{name:'安装当前 KML',exact:true}).click();await routePanel.getByRole('status').filter({hasText:routeInstallText('zh-Hans').pending}).waitFor();
 assert.equal(routeInstallCalls,1);assert.deepEqual(Uint8Array.from(routeUpload.bytes),source);
 await routePanel.getByRole('button',{name:'查询设备确认',exact:true}).click();await routePanel.getByRole('button',{name:'查询设备确认',exact:true}).waitFor();
 await page.waitForFunction(()=>!document.querySelectorAll('#tracks-shared-detail .route-install-panel button')[2]?.disabled);
 await routePanel.getByRole('button',{name:'查询设备确认',exact:true}).click();await routePanel.getByRole('status').filter({hasText:routeInstallText('zh-Hans').installed}).waitFor();
 assert.equal(routeInstallCalls,1);assert.equal(routeQueryCalls,2);
 const routeCommands=managementCalls;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);const copy=routeInstallText(locale.id);await routePanel.getByRole('heading',{name:copy.title,exact:true}).waitFor();await routePanel.getByRole('status').filter({hasText:copy.installed}).waitFor();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);assert.equal(managementCalls,routeCommands);}
 assert.equal(errors.length,0);
 await page.locator('.management-language select').selectOption('zh-Hans');
 const beforeOwnEdit=managementCalls;
 await page.locator('.tracks-editor').getByLabel(workspace.title,{exact:true}).fill('Own uploaded route');
 assert.equal(await ownInstall.getByRole('button',{name:routeInstallText('zh-Hans').install,exact:true}).isDisabled(),true);
 assert.equal(await routePanel.getByRole('button',{name:routeInstallText('zh-Hans').forget,exact:true}).isDisabled(),true);
 assert.equal(managementCalls,beforeOwnEdit);
 await page.getByRole('button',{name:workspace.save,exact:true}).click();
 await page.locator('.own-route-install').getByText('Own uploaded route',{exact:true}).waitFor();
 await page.waitForFunction(()=>!document.querySelector('.own-route-install .route-install-panel button')?.disabled);
 page.once('dialog',dialog=>dialog.accept());await ownInstall.getByRole('button',{name:routeInstallText('zh-Hans').install,exact:true}).click();
 await ownInstall.getByRole('status').filter({hasText:routeInstallText('zh-Hans').pending}).waitFor();
 const expectedOwnSource=new TextEncoder().encode(new TextDecoder().decode(source).replace('<name>Shared folder</name>','<name>Own uploaded route</name>'));
 assert.equal(routeInstallCalls,2);assert.deepEqual(Uint8Array.from(routeUpload.bytes),expectedOwnSource);
 await ownInstall.getByRole('button',{name:routeInstallText('zh-Hans').query,exact:true}).click();await ownInstall.getByRole('status').filter({hasText:routeInstallText('zh-Hans').installed}).waitFor();
 assert.equal(routeInstallCalls,2);assert.equal(routeQueryCalls,3);
 page.once('dialog',dialog=>dialog.accept());await routePanel.getByRole('button',{name:routeInstallText('zh-Hans').forget,exact:true}).click();
 await routePanel.getByRole('alert').filter({hasText:'ROUTE_CHECKPOINT_STALE'}).waitFor();
 assert.equal(routeInstallCalls,2);assert.equal(routeQueryCalls,3);
 await routePanel.getByRole('button',{name:routeInstallText('zh-Hans').query,exact:true}).click();
 await routePanel.getByRole('alert').filter({hasText:'ROUTE_CHECKPOINT_STALE'}).waitFor();
 assert.equal(routeInstallCalls,2);assert.equal(routeQueryCalls,3);
 // Reload loses the serial session and selected detail, but keeps only the
 // public installation binding. Recovery must prove the connected identity
 // and query the existing target, without uploading or installing again.
 await page.reload();await page.locator('.management-language select').selectOption('zh-Hans');
 await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();
 await routePanel.getByRole('button',{name:routeInstallText('zh-Hans').restore,exact:true}).click();
 await routePanel.getByRole('status').filter({hasText:routeInstallText('zh-Hans').installed}).waitFor();
 await routePanel.getByRole('button',{name:routeInstallText('zh-Hans').query,exact:true}).click();
 await page.waitForFunction(()=>!document.querySelectorAll('#tracks-shared-detail .route-install-panel button')[2]?.disabled);
 assert.equal(routeInstallCalls,2);assert.equal(routeQueryCalls,4);
 assert.equal(await page.locator('.shared-track-detail').count(),0);
 rejectAuthor=true;
 await routePanel.getByRole('button',{name:routeInstallText('zh-Hans').restore,exact:true}).click();
 await routePanel.getByRole('alert').filter({hasText:routeInstallText('zh-Hans').failed}).waitFor();
 const failedCommands=managementCalls;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await routePanel.getByRole('alert').filter({hasText:routeInstallText(locale.id).failed}).waitFor();assert.equal(managementCalls,failedCommands);}
 assert.equal(routeInstallCalls,2);assert.equal(routeQueryCalls,4);
 assert.equal(errors.length,0);
 console.log('Current tracks page IP UI: public map/regions/ID/detail/download/copy without Web Serial or USB; connect/disconnect retains public views and in-flight reads; owned pagination, KML resources, 11 locales/mobile, update/archive and USB route installation/recovery passed; RF directory commands forbidden.');
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
