import {chromium} from '../../site/node_modules/playwright/index.mjs';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {decode,encode} from '../../site/geocaching/src/protocol.js';
import {FrameDecoder,encodeFrame,FRAME} from '../../site/device/src/hostlink.js';
import {decodeManagement,encodeManagement} from '../../site/device/src/management-protocol.js';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {locales} from '../../site/i18n/registry.js';
import {deviceRecordText} from '../../site/i18n/device-record-labels.js';
import {trackDirectoryText} from '../../site/i18n/track-directory-labels.js';
import {kmlActionText} from '../../site/i18n/kml-action-labels.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
const directoryIdentity=await Identity.generate(),directoryPublic=await directoryIdentity.getPublicKey();
const directoryDelivery=await Identity.truncatedHash(Uint8Array.from([...(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),...directoryIdentity.identityHash]));
const authorIdentity=await Identity.generate(),authorPublic=await authorIdentity.getPublicKey();
const authorDelivery=await Identity.truncatedHash(Uint8Array.from([...(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),...authorIdentity.identityHash]));
const authorInfo=[authorPublic,authorIdentity.identityHash,authorDelivery,7];
let directoryReady=false,directoryEpoch=new Uint8Array(16).fill(8),directoryBad=false;
const discoveryRequests=[];
const root=process.cwd(),server=http.createServer(async(req,res)=>{try{let pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);if(pathname.endsWith('/'))pathname+='index.html';const filename=path.resolve(root,'.'+pathname);if(!filename.startsWith(root+path.sep))throw Error('path');const contents=await fs.readFile(filename);res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.webp':'image/webp','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(filename)]??'application/octet-stream');res.end(contents);}catch{res.writeHead(404).end();}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${server.address().port}`;
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true}),page=await browser.newPage({viewport:{width:1440,height:1000}});
const errors=[];page.on('pageerror',error=>errors.push(String(error)));
let support=true,badCapabilities=false,media=1,catalogCalls=0,snapshots=false,slowBuild=false,badHash=false,cancelCalls=0,releaseCalls=0,releaseFails=false;
let managementCalls=0,holdSnapshot=false,snapshotWaiting=false,resumeSnapshot;
let holdDirectory=false,directoryWaiting=false,resumeDirectory;
const recordingSource=new TextEncoder().encode('<gpx version="1.1" creator="Trail Mate"><trk><name>USB 山径</name><trkseg><trkpt lat="30" lon="120"><ele>100</ele><time>2026-10-11T00:00:00Z</time></trkpt><trkpt lat="30.1" lon="120.1"><ele>110</ele><time>2026-10-11T00:01:00Z</time></trkpt></trkseg></trk></gpx>');
const recordingHash=new Uint8Array(await crypto.subtle.digest('SHA-256',recordingSource));
const nativeHex=execFileSync(path.resolve('.codex-build/hostlink-codec/native/track_catalog'+(process.platform==='win32'?'.exe':'')),{encoding:'utf8'}).trim();const nativeBytes=Uint8Array.from(Buffer.from(nativeHex,'hex')),nativeBody=decode(nativeBytes,512);assert.deepEqual(encode(nativeBody),nativeBytes);
let records=Array.from({length:10},(_,index)=>index===0?nativeBody[2]:[`山径 ${index}.csv`,2,1000+index,0]);
await page.exposeFunction('trackDeviceReply',async raw=>{
 const frame=new FrameDecoder().push(Uint8Array.from(raw))[0];assert.ok(frame);
 if(frame.type===FRAME.HELLO)return [...encodeFrame(FRAME.HELLO_ACK,frame.sequence,new Uint8Array([1,0,0,2,0,4,0,0,6,...new TextEncoder().encode('T-Deck'),4,...new TextEncoder().encode('test')]))];
 assert.equal(frame.type,FRAME.MANAGEMENT_REQUEST);++managementCalls;const request=decodeManagement(frame.payload);let body,status=0;
 if(request.code===0)body=badCapabilities?[0]:[1,512,320,1,support?(snapshots?[0,1,2,32,33,35,80,81,82,86,87,88,89,90]:[0,1,2,32,33,35,80,82,89,90]):[0],7];
 else if(request.code===1)body=authorInfo;
 else if(request.code===2){assert.equal(request.body[1],7);body=[...authorInfo,await authorIdentity.sign(authorProofMessage(request.session,request.body[0],authorPublic,7))];}
 else if(request.code===90){assert.equal(request.body.length,1);assert.equal(request.body[0].length,16);discoveryRequests.push([...request.body[0]]);body=[];}
 else if(request.code===89){if(holdDirectory){directoryWaiting=true;await new Promise(resolve=>resumeDirectory=resolve);directoryWaiting=false;}body=[null,[[directoryBad?new Uint8Array(16).fill(7):directoryDelivery,directoryPublic,directoryEpoch,'林间目录',directoryReady?1:0,directoryReady?8388608:0,directoryReady?1024:0,directoryReady?32:0]]];}
 else if(request.code===80){catalogCalls++;const cursor=request.body[0];if(cursor&&cursor[0]!==media){status=4;body=[];}else{const index=cursor?cursor[1]/32-3:0;body=index===0&&media===1?nativeBody:index<records.length?[media,(index+4)*32,records[index]]:[media,null,null];}}
 else if(request.code===86){const [id,requestedMedia,name,count,format]=request.body;assert.equal(requestedMedia,media);assert.equal(name,'import.gpx');assert.equal(count,recordingSource.length);assert.equal(format,1);body=[id,slowBuild?1:3,slowBuild?0:count,count,format];}
 else if(request.code===87){cancelCalls++;body=[];}
 else if(request.code===88){releaseCalls++;body=[];if(releaseFails)status=6;}
 else if(request.code===81){const [id,offset,length]=request.body;if(holdSnapshot){snapshotWaiting=true;await new Promise(resolve=>resumeSnapshot=resolve);snapshotWaiting=false;}body=[id,recordingSource.length,badHash?new Uint8Array(32):recordingHash,1,recordingSource.slice(offset,offset+length),offset];}
 else throw Error('Unexpected management command');
 return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,status,body))];
});
await page.addInitScript(()=>{
 let input;window.trackPortCloses=0;
 const port={async open(){this.readable=new ReadableStream({start(controller){input=controller;}});this.writable=new WritableStream({async write(bytes){const response=await window.trackDeviceReply([...bytes]);try{input.enqueue(Uint8Array.from(response));}catch{}}});},async setSignals(){},async close(){window.trackPortCloses++;}};
 Object.defineProperty(navigator,'serial',{configurable:true,value:{async requestPort(){return port;}}});
});
try{
 await page.exposeFunction('isSnapshotWaiting',()=>snapshotWaiting);
 await page.exposeFunction('isDirectoryWaiting',()=>directoryWaiting);
 await page.goto(origin+'/site/tracks/?lang=zh-Hans');await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();
 await page.getByText('已读取一批设备记录，可继续加载。',{exact:true}).waitFor();assert.equal(await page.locator('.device-record-row').count(),8);assert.ok((await page.locator('.device-record-row').first().innerText()).includes('正在录制'));
 const listCalls=managementCalls,listNames=await page.locator('.device-record-row strong').allTextContents(),portCloses=await page.evaluate(()=>trackPortCloses);
 for(const locale of locales){const text=deviceRecordText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByRole('button',{name:text.refresh,exact:true}).waitFor();await page.getByText(text.listMore,{exact:true}).waitFor();assert.deepEqual(await page.locator('.device-record-row strong').allTextContents(),listNames);assert.equal(managementCalls,listCalls);assert.equal(await page.evaluate(()=>trackPortCloses),portCloses);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 assert.equal(await page.locator('.tracks-row').count(),0,'Device records entered browser draft list');
 await page.getByRole('region',{name:'电脑网络',exact:true}).waitFor();
 await page.locator('.track-network-advanced > summary').click();
 await page.getByLabel('WSS 连接地址',{exact:true}).waitFor();
 await page.locator('.track-network-advanced > summary').click();
 assert.equal(await page.getByRole('button',{name:'刷新轨迹目录',exact:true}).count(),0);assert.equal(discoveryRequests.length,0);assert.equal(managementCalls,listCalls);
 await page.evaluate(()=>{scrollTo(0,0);document.querySelector('.sidebar').scrollTop=0;});
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-track-directories-wide.png',fullPage:true});
 media=2;await page.getByRole('button',{name:'加载更多设备记录',exact:true}).click();await page.getByText('设备记录列表已变化，请刷新',{exact:true}).waitFor();assert.equal(await page.locator('.device-record-row').count(),8);
 await page.getByRole('button',{name:'刷新设备记录',exact:true}).click();await page.getByText('已读取一批设备记录，可继续加载。',{exact:true}).waitFor();
 await page.getByRole('button',{name:'加载更多设备记录',exact:true}).click();await page.getByText('设备记录列表已读完；录制或删除后可刷新。',{exact:true}).waitFor();assert.equal(await page.locator('.device-record-row').count(),10);
 assert.equal(await page.getByRole('button',{name:'加载更多设备记录',exact:true}).count(),0);
 await page.evaluate(()=>{scrollTo(0,0);document.querySelector('.sidebar').scrollTop=0;document.querySelector('.device-records-list').scrollTop=0;});
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-track-catalog.png',fullPage:true});
 await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-track-directories-mobile.png',fullPage:true});
 await page.getByRole('button',{name:'断开设备',exact:true}).click();await page.getByText('设备已断开。',{exact:true}).waitFor();assert.equal(await page.locator('.device-record-row').count(),0);
 support=false;const before=catalogCalls;await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await page.getByText('此固件尚不支持读取设备记录列表。',{exact:true}).waitFor();assert.equal(catalogCalls,before);assert.equal(await page.getByRole('button',{name:'刷新设备记录',exact:true}).isDisabled(),true);
 assert.equal(await page.getByRole('button',{name:'刷新目录',exact:true}).isDisabled(),true);
 await page.getByRole('button',{name:'断开设备',exact:true}).click();badCapabilities=true;
 const closes=await page.evaluate(()=>window.trackPortCloses);await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await page.getByText('设备管理能力响应不兼容',{exact:true}).waitFor();assert.equal(await page.evaluate(()=>window.trackPortCloses),closes+1);assert.equal(await page.getByRole('button',{name:'连接设备查看记录',exact:true}).isEnabled(),true);
 assert.deepEqual(errors,[]);console.log('Actual track page device catalogue passed USB framing, pagination, media change, capability gating, disconnect cleanup and mobile layout.');
 let allowDiscard=true;
 badCapabilities=false;support=true;snapshots=true;records[1]=['import.gpx',1,recordingSource.length,0];page.on('dialog',dialog=>dialog.message().includes('放弃未保存')&&!allowDiscard?dialog.dismiss():dialog.accept());
 await page.setViewportSize({width:1440,height:1000});await page.getByRole('button',{name:'连接设备查看记录',exact:true}).click();await page.getByText('已读取一批设备记录，可继续加载。',{exact:true}).waitFor();
 assert.equal(await page.getByRole('button',{name:'导入设备记录 live.GPX',exact:true}).isDisabled(),true);
 holdSnapshot=true;await page.getByRole('button',{name:'导入设备记录 import.gpx',exact:true}).click();await page.waitForFunction(async()=>await window.isSnapshotWaiting());
 const readCalls=managementCalls;
 for(const locale of locales){const text=deviceRecordText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByRole('button',{name:text.cancelImport,exact:true}).waitFor();assert.equal(managementCalls,readCalls);assert.equal(await page.locator('.tracks-row').count(),0);}
 await page.locator('.management-language select').selectOption('zh-Hans');holdSnapshot=false;resumeSnapshot();
 await page.getByText('设备记录已导入浏览器草稿，尚未发布到网络。',{exact:true}).waitFor();assert.equal(await page.locator('.tracks-row').count(),1);
 const downloadPromise=page.waitForEvent('download');await page.getByRole('button',{name:'下载 KML',exact:true}).click();const downloaded=await downloadPromise;assert.ok((await fs.readFile(await downloaded.path(),'utf8')).includes('<gx:Track>'));
 const originalPromise=page.waitForEvent('download');await page.getByRole('link',{name:'下载原始记录 import.gpx',exact:true}).click();const original=await originalPromise;assert.equal(original.suggestedFilename(),'import.gpx');assert.deepEqual(new Uint8Array(await fs.readFile(await original.path())),recordingSource);
 await page.evaluate(()=>{scrollTo(0,0);document.querySelector('.sidebar').scrollTop=0;document.querySelector('.device-records-list').scrollTop=0;});
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-track-import.png',fullPage:true});
 slowBuild=true;allowDiscard=false;await page.getByRole('button',{name:'导入设备记录 import.gpx',exact:true}).click();await page.getByRole('button',{name:'取消设备记录导入',exact:true}).waitFor();await page.getByLabel('要素名称',{exact:true}).fill('读取时的新编辑');slowBuild=false;
 await page.getByText('未导入设备记录，请查看工作台提示。',{exact:true}).waitFor();assert.equal(await page.locator('.tracks-row').count(),1);assert.equal(await page.getByLabel('要素名称',{exact:true}).inputValue(),'读取时的新编辑');allowDiscard=true;
 assert.equal(await page.getByRole('button',{name:'导入设备记录 import.gpx',exact:true}).isDisabled(),true);
 await page.getByRole('button',{name:'保存浏览器草稿',exact:true}).click();
 await page.getByText(kmlActionText('zh-Hans').saved,{exact:true}).waitFor();
 slowBuild=true;await page.getByRole('button',{name:'导入设备记录 import.gpx',exact:true}).click();await page.getByRole('button',{name:'取消设备记录导入',exact:true}).waitFor();await page.getByRole('button',{name:'取消设备记录导入',exact:true}).click();await page.getByText('已取消设备记录导入。',{exact:true}).waitFor();assert.equal(cancelCalls,1);assert.equal(await page.locator('.tracks-row').count(),1);
 slowBuild=false;badHash=true;await page.getByRole('button',{name:'导入设备记录 import.gpx',exact:true}).click();await page.getByText('设备轨迹快照哈希不匹配',{exact:true}).waitFor();assert.equal(await page.locator('.tracks-row').count(),1);badHash=false;
 await page.waitForFunction(()=>document.querySelector('[aria-label="导入设备记录 import.gpx"]')?.disabled===false);assert.equal(releaseCalls,3);
 releaseFails=true;await page.getByRole('button',{name:'导入设备记录 import.gpx',exact:true}).click();await page.getByText(/设备临时快照未确认回收/).waitFor();assert.equal(await page.locator('.tracks-row').count(),2);assert.equal(releaseCalls,4);
 assert.equal(await page.getByRole('button',{name:'导入设备记录 import.gpx',exact:true}).isDisabled(),true);
 const pendingCalls=managementCalls,pendingReleaseCalls=releaseCalls;
 for(const locale of locales){const text=deviceRecordText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByRole('button',{name:text.retryRelease,exact:true}).waitFor();assert.equal(await page.getByRole('button',{name:text.importLabel.replace('{name}','import.gpx'),exact:true}).isDisabled(),true);assert.equal(await page.getByRole('link',{name:text.download+' import.gpx',exact:true}).count(),1);assert.equal(releaseCalls,pendingReleaseCalls);assert.equal(managementCalls,pendingCalls);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 releaseFails=false;await page.getByRole('button',{name:'重试回收临时快照',exact:true}).click();await page.getByText('设备临时快照已回收，浏览器草稿保留。',{exact:true}).waitFor();assert.equal(await page.locator('.tracks-row').count(),2);assert.equal(releaseCalls,5);assert.equal(await page.getByRole('button',{name:'重试回收临时快照',exact:true}).count(),0);
 await page.getByRole('button',{name:'清除原始记录缓存',exact:true}).click();assert.equal(await page.getByRole('link',{name:'下载原始记录 import.gpx',exact:true}).count(),0);assert.equal(await page.locator('.tracks-row').count(),2);
 assert.deepEqual(errors,[]);console.log('Actual USB snapshot import passed capabilities, recording guard, SHA verification, GPX conversion, download, cancellation, hash rejection and sealed snapshot release.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
