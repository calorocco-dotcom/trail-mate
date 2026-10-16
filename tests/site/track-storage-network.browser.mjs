// Real production browser protocol over an explicitly supplied loopback gateway.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import {chromium} from '../../site/node_modules/playwright/index.mjs';
const [endpoint,seed,marker]=process.argv.slice(2);
if(!/^ws:\/\/127\.0\.0\.1:\d+\/$/.test(endpoint)||!/^[a-f0-9]{32}$/.test(seed)||!path.isAbsolute(marker))throw Error('Explicit local test parameters required');
const root=path.resolve('.codex-build/hostlink-codec/track-storage-network/browser');await fs.mkdir(root,{recursive:true});
await fs.writeFile(path.join(root,'NOTES.txt'),'Actual browser client/authenticated native storage-503 recovery. Isolated local test SQLite and gateway. No USB/RF/public publication. Current source reusable bundle, retain compact result until acceptance.');
const source=`import{TrackDirectoryClient}from'./tracks/src/protocol-client.js';import{Identity,toHex}from'@reticulum/core';
window.runStorageNetwork=async({endpoint,seed})=>{const client=new TrackDirectoryClient();try{
 await client.connect(endpoint,null,[seed]);const until=Date.now()+20000;let rows=[];while(Date.now()<until){rows=await client.verifiedDirectories();if(rows.length)break;await new Promise(resolve=>setTimeout(resolve,100));}if(rows.length!==1)throw Error('Verified directory required');
 const selected=rows[0],entry=client.directories.get(selected.key),requestId=new Uint8Array(16).fill(17),data=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'),digest=await Identity.fullHash(data),failures=[];
 for(let i=0;i<3;i++){try{await client.enqueue(()=>client.request(entry,4,[digest,data.length],{requestId}));throw Error('Read-only write accepted');}catch(error){if(error.code!=='TRACK_DIRECTORY_STORAGE_UNAVAILABLE'||error.status!==503||error.reason!=='storage unavailable')throw error;failures.push({code:error.code,status:error.status});}}
 const before=await client.enqueue(()=>client.request(entry,0,[]));await window.recoverStorage();
 const upload=await client.enqueue(()=>client.request(entry,4,[digest,data.length],{requestId})),same=await client.enqueue(()=>client.request(entry,4,[digest,data.length],{requestId}));
 if(toHex(upload[0])!==toHex(same[0]))throw Error('Original upload not resumed');
 await client.enqueue(()=>client.request(entry,5,[upload[0],0,data]));const sealed=await client.enqueue(()=>client.request(entry,6,[upload[0]]));
 if(toHex(sealed[0])!==toHex(digest)||sealed[1]!==data.length||sealed[2]!==1)throw Error('File not sealed');
 const after=await client.enqueue(()=>client.request(entry,0,[])),content=await client.enqueue(()=>client.request(entry,8,[digest,0,data.length]));
 if(toHex(before[2])!==toHex(after[2])||toHex(before[3])!==toHex(after[3])||toHex(await Identity.fullHash(content[3]))!==toHex(digest))throw Error('Storage recovery changed identity/epoch/content');
 return{failures,capabilityDuringFault:true,originalUploadRecovered:true,sealed:true,directoryUnchanged:true,contentHash:toHex(digest)};
}finally{await client.close();}};`;
await build({stdin:{contents:source,loader:'js',resolveDir:path.resolve('site')},bundle:true,format:'esm',outfile:path.join(root,'page.js'),logLevel:'silent'});
const server=http.createServer(async(req,res)=>{if(req.url==='/page.js'){res.setHeader('Content-Type','text/javascript');res.end(await fs.readFile(path.join(root,'page.js')));}else{res.setHeader('Content-Type','text/html');res.end('<!doctype html><script type="module" src="/page.js"></script>');}});
await new Promise(resolve=>server.listen(Number(process.env.TRAIL_MATE_STORAGE_PAGE_PORT??0),'127.0.0.1',resolve));
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
try{
 const page=await browser.newPage(),errors=[];page.on('pageerror',error=>errors.push(String(error)));
 await page.exposeFunction('recoverStorage',async()=>{assert.deepEqual(await fs.readdir(path.join(path.dirname(marker),'service','track-data','uploads')),[]);await fs.writeFile(marker,'Restore isolated test SQLite writes');});
 await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>typeof window.runStorageNetwork==='function');
 const result=await page.evaluate(options=>window.runStorageNetwork(options),{endpoint,seed});assert.deepEqual(result.failures.map(row=>row.status),[503,503,503]);assert.deepEqual(errors,[]);
 await fs.writeFile(path.join(root,'result.json'),JSON.stringify({passed:true,actualBrowser:true,nativeNetwork:true,physicalUsb:false,publicNetwork:false,...result},null,2));
 console.log('Actual native storage 503/authenticated browser mapping, capabilities during failure, no orphan uploads, fixed-ID recovery, exact file seal/read and stable directory identity/epoch passed.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
