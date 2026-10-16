import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import http from 'node:http';import path from 'node:path';import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';
import {FrameDecoder,encodeFrame,FRAME} from '../../site/device/src/hostlink.js';
import {decodeManagement,encodeManagement} from '../../site/device/src/management-protocol.js';
import {encode} from '../../site/geocaching/src/protocol.js';
const bundled=await build({stdin:{contents:`import {HostlinkConnection} from './device/src/hostlink.js';import {ManagementSession} from './device/src/management-protocol.js';import {uploadTrackFile,uploadTrackManifest,queryTrackUpload,discardTrackUpload} from './tracks/src/device-upload.js';import {encodeTrackManifest} from './tracks/src/manifest.js';window.uploadTest={HostlinkConnection,ManagementSession,uploadTrackFile,uploadTrackManifest,encodeTrackManifest,queryTrackUpload,discardTrackUpload};`,resolveDir:path.resolve('site')},bundle:true,write:false,format:'iife',target:'es2022'});
const server=http.createServer((req,res)=>{res.setHeader('Content-Type',req.url==='/bundle.js'?'text/javascript':'text/html');res.end(req.url==='/bundle.js'?bundled.outputFiles[0].text:'<!doctype html><script src="/bundle.js"></script>');});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
const page=await browser.newPage(),errors=[];page.on('pageerror',error=>errors.push(String(error)));
const native=path.resolve('.codex-build/hostlink-codec/native/upload_protocol'+(process.platform==='win32'?'.exe':''));
let content=[],total=0,expectedHash,finishPolls=0,abortCalls=0,closed=0,savedKml=[],kind=3,sealChecks=0;
await page.exposeFunction('uploadReply',async raw=>{
 const frame=new FrameDecoder().push(Uint8Array.from(raw))[0];assert.ok(frame);
 if(frame.type===FRAME.HELLO)return [...encodeFrame(FRAME.HELLO_ACK,frame.sequence,new Uint8Array([1,0,0,2,0,4,0,0,6,...new TextEncoder().encode('T-Deck'),4,...new TextEncoder().encode('test')]))];
 const request=decodeManagement(frame.payload);let body,status=0;
 const nativeCommand={32:'begin',33:'chunk',35:'finish'}[request.code];
 if(nativeCommand)execFileSync(native,[nativeCommand,Buffer.from(encode(request.body)).toString('hex')],{encoding:'utf8'});
 if(request.code===32){assert.ok([3,4].includes(request.body[1]));if(request.body[1]===4&&kind===3){assert.equal(finishPolls,2);savedKml=content.slice();content=[];finishPolls=0;kind=4;}total=request.body[2];expectedHash=request.body[3];body=[request.body[0],320,content.length,0];}
 else if(request.code===33){assert.equal(request.body[1],content.length);assert.ok(request.body[2].length<=320);content.push(...request.body[2]);body=[request.body[0],320,content.length,0];}
 else if(request.code===35){assert.equal(content.length,total);assert.deepEqual(new Uint8Array(await crypto.subtle.digest('SHA-256',new Uint8Array(content))),expectedHash);finishPolls++;sealChecks++;body=[request.body[0],320,content.length,finishPolls>=2?1:0];}
 else if(request.code===34)body=[request.body[0],320,content.length,1];
 else if(request.code===36){abortCalls++;if(abortCalls===1)status=6;else content=[];body=[];}
 else throw Error('Unexpected upload operation');
 return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,status,body))];
});
await page.exposeFunction('uploadPortClosed',()=>{closed++;});
await page.addInitScript(()=>{
 let input;
 const port={async open(){this.readable=new ReadableStream({start(controller){input=controller;}});this.writable=new WritableStream({async write(bytes){input.enqueue(Uint8Array.from(await window.uploadReply([...bytes])));}});},async setSignals(){},async close(){await window.uploadPortClosed();}};
 Object.defineProperty(navigator,'serial',{configurable:true,value:{async requestPort(){return port;}}});
});
try{
 await page.goto(`http://127.0.0.1:${server.address().port}`);
 const result=await page.evaluate(async()=>{
  const api=window.uploadTest;
  const id=new Uint8Array(16).fill(5),source=Uint8Array.from({length:750},(_,index)=>index%256),progress=[];
  const firstConnection=new api.HostlinkConnection(await navigator.serial.requestPort());await firstConnection.open();const firstSession=new api.ManagementSession(firstConnection),controller=new AbortController();let paused=false;
  try{
   await api.uploadTrackFile(firstSession,source,{id,signal:controller.signal,onProgress:value=>{if(value.offset===320)controller.abort(Error('paused'));}});
  }catch(error){paused=error.message==='paused';}finally{firstSession.close();await firstConnection.close();}
  const connection=new api.HostlinkConnection(await navigator.serial.requestPort());await connection.open();const session=new api.ManagementSession(connection);
  try{
   const receipt=await api.uploadTrackFile(session,source,{id,finishPollMs:0,onProgress:value=>progress.push(value)});
   const state=await api.queryTrackUpload(session,id,750);
   const manifestId=new Uint8Array(16).fill(6),fileHash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
   const manifest=api.encodeTrackManifest([1,new Uint8Array(64).fill(2),new Uint8Array(16).fill(3),0,1,null,0,1,1,'test','',[0,0,0,0],[0,null,null,1],[fileHash,750,'application/vnd.google-earth.kml+xml',1]]);
   const manifestReceipt=await api.uploadTrackManifest(session,manifest,{id:manifestId,finishPollMs:0});
   const manifestState=await api.queryTrackUpload(session,manifestId,manifest.length);let rejected=false;
   try{await api.discardTrackUpload(session,manifestId);}catch(error){rejected=error.code===6;}
   const discarded=await api.discardTrackUpload(session,manifestId);
   return {paused,resumedOffset:progress[0].offset,bytes:receipt.bytes,sealed:receipt.sealed,published:receipt.published,offset:state.offset,validating:progress.some(value=>value.stage==='validating'),rejected,discarded,manifestSealed:manifestReceipt.sealed,manifestUnpublished:manifestReceipt.published===false,manifestComplete:manifestState.offset===manifest.length};
  }finally{session.close();await connection.close();}
 });
 assert.deepEqual(result,{paused:true,resumedOffset:320,bytes:750,sealed:true,published:false,offset:750,validating:true,rejected:true,discarded:true,manifestSealed:true,manifestUnpublished:true,manifestComplete:true});
 assert.equal(finishPolls,2);assert.equal(sealChecks,4);assert.equal(abortCalls,2);assert.equal(content.length,0);assert.equal(closed,2);assert.deepEqual(errors,[]);
 assert.deepEqual(savedKml,Array.from({length:750},(_,index)=>index%256));
 console.log('Edge track upload passed real HostLink framing, management session, native request decode, SHA, async seal, status and discard retry. Device/SD responses remain simulated.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
