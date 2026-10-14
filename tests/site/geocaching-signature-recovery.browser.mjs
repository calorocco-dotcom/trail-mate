// Actual built page: migration from a retained IP signature to the original USB
// correlation, then reload. Synthetic author only; no physical/public writes.
import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {FrameDecoder,encodeFrame,FRAME} from '../../site/device/src/hostlink.js';
import {decodeManagement,encodeManagement} from '../../site/device/src/management-protocol.js';
import {equal} from '../../site/geocaching/src/protocol.js';
import {buildPublicationRecord} from '../../site/geocaching/src/publication-record.js';
import {saveIPPublication} from '../../site/geocaching/src/ip-publication-journal.js';
import {geocachingSaveRecoveryText} from '../../site/i18n/geocaching-save-recovery-labels.js';
import http from 'node:http';import fs from 'node:fs/promises';import path from 'node:path';import assert from 'node:assert/strict';
const root=path.resolve('site'),server=http.createServer(async(req,res)=>{try{const name=new URL(req.url,'http://localhost').pathname;if(!name.startsWith('/trail-mate/'))throw Error('prefix');const file=path.resolve(root,name.slice(12)+(name.endsWith('/')?'index.html':''));if(!file.startsWith(root+path.sep))throw Error('path');res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.svg':'image/svg+xml','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(file)]??'application/octet-stream');res.end(await fs.readFile(file));}catch{res.writeHead(404).end();}});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
const author=await Identity.generate(),publicKey=await author.getPublicKey(),nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),destination=await Identity.truncatedHash(Uint8Array.from([...nameHash,...author.identityHash]));
const draftId=new Uint8Array(16).fill(3),transferId=new Uint8Array(16).fill(4),originalOwner=new Uint8Array(16).fill(9);
const publication=await buildPublicationRecord({id:draftId,state:0,latitudeE7:310000000,longitudeE7:1210000000,name:'恢复签名输入',description:'保留正文',hint:'',difficultyX2:2,terrainX2:2,containerSize:0,authorKey:null,baseHash:null},{verified:true,publicKey,generation:7},{exists:false},{nowSeconds:1700000000});
const hash=await Identity.fullHash(publication.raw);let checkpointKey,checkpointText;
saveIPPublication({stage:'signing',authorKey:publicKey,authorGeneration:7,draftId,draftGeneration:1,operationId:new Uint8Array(16).fill(5),transferId,hash,raw:publication.raw,signature:null,directoryKey:'6'.repeat(32)},{storage:{getItem(){return null;},setItem(key,text){checkpointKey=key;checkpointText=text;}}});
const calls=[],browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true}),context=await browser.newContext({viewport:{width:1440,height:960}});
let busy=true,mismatch=false;
await context.exposeFunction('deviceReply',async raw=>{
 const frame=new FrameDecoder().push(Uint8Array.from(raw))[0];assert.ok(frame);
 if(frame.type===FRAME.HELLO)return [...encodeFrame(FRAME.HELLO_ACK,frame.sequence,new Uint8Array([1,0,0,2,255,31,0,0,6,...new TextEncoder().encode('T-Deck'),4,...new TextEncoder().encode('test')]))];
 const request=decodeManagement(frame.payload),body=request.body;calls.push({code:request.code,owner:request.session.slice()});let response,status=0;
 if(request.code===0)response=[1,512,320,1,[0,1,2,3,38,64,65,71,72,73],7];
 else if(request.code===1)response=[publicKey,author.identityHash,destination,7];
 else if(request.code===2)response=[publicKey,author.identityHash,destination,7,await author.sign(authorProofMessage(request.session,body[0],publicKey,7))];
 else if(request.code===3)response=[];
 else if(request.code===38){assert.deepEqual(body,[5,null]);if(busy){busy=false;status=5;response=[];}else response=[1,null,[transferId,5,publication.raw.length,mismatch?new Uint8Array(32):hash,7,1,publication.raw.length,originalOwner,draftId]];}
 else if(request.code===64){assert.ok(equal(request.session,originalOwner));response=[1,0,0,null];}
 else throw Error('Unexpected mutation during recovery: '+request.code);
 return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,status,response))];
});
await context.addInitScript(()=>{window.Worker=class{postMessage(){}terminate(){}};Object.defineProperty(navigator,'serial',{value:{async requestPort(){let controller;return {async open(){},async setSignals(){},readable:new ReadableStream({start(value){controller=value;}}),writable:new WritableStream({async write(bytes){controller.enqueue(Uint8Array.from(await window.deviceReply([...bytes])));}}),async close(){}};}}});});
const page=await context.newPage(),errors=[];page.on('pageerror',error=>errors.push(error.message));
try{
 await page.goto(origin+'/trail-mate/geocaching/?lang=zh-Hans');
 await page.evaluate(({key,text})=>{localStorage.setItem(key,text);localStorage.setItem('trailmate.geo.usb-owner.v1','8'.repeat(32));},{key:checkpointKey,text:checkpointText});
 async function connect(){await page.getByRole('button',{name:'我的藏宝点',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('设备作者已验证。目录连接状态独立显示。',{exact:true}).waitFor();}
 await connect();assert.equal(calls.filter(row=>row.code===38).length,2);assert.ok(equal(calls.filter(row=>row.code===2).at(-1).owner,originalOwner));
 await page.getByRole('button',{name:'刷新设备列表',exact:true}).click();await page.getByRole('button',{name:'刷新设备列表',exact:true}).click({trial:true});assert.equal(calls.at(-2).code,64);
 await page.reload();await connect();assert.ok(equal(calls.filter(row=>row.code===2).at(-1).owner,originalOwner));
 assert.equal(await page.evaluate(key=>localStorage.getItem(key),checkpointKey),checkpointText);
 await page.getByRole('button',{name:'断开 USB',exact:true}).click();mismatch=true;
 await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText(geocachingSaveRecoveryText('zh-Hans').ownerConflict,{exact:true}).waitFor();
 assert.ok(calls.every(row=>[0,1,2,3,38,64].includes(row.code)));assert.deepEqual(errors,[]);
 console.log('Actual Geocaching page: retained signing owner recovered after Busy, reload kept correlation, changed hash blocked connection; zero writes.');
}catch(error){console.error(JSON.stringify({operations:calls.map(row=>row.code),status:await page.locator('.geo-maintenance-list').textContent().catch(()=>null)}));throw error;}
finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
