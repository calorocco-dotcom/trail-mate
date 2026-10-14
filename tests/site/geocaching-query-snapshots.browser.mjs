// Actual browser RNS/LXMF client, native SQLite directory and packet bridge.
// Capacity=1 and 21 records force partial pages. No USB/RF/public publication.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import http from 'node:http';
import net from 'node:net';
import path from 'node:path';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import {chromium} from '../../site/node_modules/playwright/index.mjs';

const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv/'+(process.platform==='win32'?'Scripts/python.exe':'bin/python'));
const work=path.resolve('.codex-build/hostlink-codec/query-snapshots-browser');
await fs.mkdir(work,{recursive:true});
await fs.writeFile(path.join(work,'NOTES.txt'),'Actual browser/native query lease replacement and paging. Current source, capacity 1, 21 isolated signed fixture records. No physical device, public publication or RF. Retain compact current evidence; remove reusable build after acceptance.');
const state=await fs.mkdtemp(path.join(work,'state-'));assert.equal(path.dirname(state),work);
const children=[],sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function start(label,args){const child=spawn(python,args,{windowsHide:true,stdio:['ignore','pipe','pipe']}),entry={child,label,output:''};child.stdout.on('data',bytes=>{entry.output+=bytes});child.stderr.on('data',bytes=>{entry.output+=bytes});entry.done=new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',resolve)});children.push(entry);return entry;}
async function ready(entry,event){const until=Date.now()+25000;while(Date.now()<until){for(const line of entry.output.split('\n'))try{const row=JSON.parse(line);if(row.event===event)return row}catch{}if(entry.child.exitCode!==null)throw Error(entry.output);await sleep(100)}throw Error('Service did not become ready: '+entry.output);}
async function freePort(){const server=net.createServer();await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const port=server.address().port;await new Promise(resolve=>server.close(resolve));return port;}
const source=`import{DirectoryClient}from'./geocaching/src/protocol-client.js';
const events=[];let client;
window.connect=async(endpoint,seeds)=>{client=new DirectoryClient(event=>events.push({type:event.type}));await client.connect(endpoint,null,seeds);};
window.hasDirectory=()=>[...client.directories.values()].some(entry=>entry.ready&&entry.queryReleaseSupported);
window.query=async({refresh=false,bounds=[-90,-180,90,180]}={})=>{await client.query(bounds,3,null,0,{refresh});return{rows:client.rows.size,more:client.pages.length,leases:client.querySnapshots.size};};
window.more=async()=>{await client.more();return{rows:client.rows.size,more:client.pages.length,leases:client.querySnapshots.size};};
window.closeClient=async()=>{await client.close();return{closed:client.closed,online:client.interface.online,leases:client.querySnapshots.size};};`;
await build({stdin:{contents:source,loader:'js',resolveDir:path.resolve('site')},bundle:true,format:'esm',outfile:path.join(work,'page.js'),logLevel:'silent'});
const server=http.createServer(async(req,res)=>{if(req.url==='/page.js'){res.setHeader('Content-Type','text/javascript');res.end(await fs.readFile(path.join(work,'page.js')))}else{res.setHeader('Content-Type','text/html');res.end('<!doctype html><script type="module" src="/page.js"></script>')}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
let browser,service;
try{
 const port=await freePort();service=start('directory',['tools/geocaching/browser_test_service.py','--state',state,'--port',String(port),'--startup-delay','0','--query-capacity','1','--browse-fixture-count','19','--run-seconds','120']);
 const directory=await ready(service,'ready'),bridge=start('bridge',['tools/geocaching/packet_bridge.py','--tcp-port',String(port),'--port','0','--origin',origin,'--run-seconds','120']);const connection=await ready(bridge,'bridge_ready');
 browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});const page=await browser.newPage(),errors=[];page.on('pageerror',error=>errors.push(String(error)));await page.goto(origin);await page.waitForFunction(()=>typeof window.connect==='function');
 await page.evaluate(({endpoint,seeds})=>window.connect(endpoint,seeds),{endpoint:'ws://127.0.0.1:'+connection.port+'/',seeds:[directory.discovery]});await page.waitForFunction(()=>window.hasDirectory(),null,{timeout:20000});
 const partial={rows:20,more:1,leases:1};assert.deepEqual(await page.evaluate(()=>window.query()),partial);
 for(let index=0;index<8;index++)assert.deepEqual(await page.evaluate(()=>window.query({refresh:true})),partial);
 assert.deepEqual(await page.evaluate(()=>window.query({bounds:[20,110,40,130]})),partial);
 assert.deepEqual(await page.evaluate(()=>window.query({bounds:[50,140,60,150]})),{rows:0,more:0,leases:0});
 assert.deepEqual(await page.evaluate(()=>window.query()),partial);
 assert.deepEqual(await page.evaluate(()=>window.more()),{rows:21,more:0,leases:0});
 assert.deepEqual(await page.evaluate(()=>window.closeClient()),{closed:true,online:false,leases:0});assert.deepEqual(errors,[]);
 const trace=service.output.split('\n').flatMap(line=>{try{const row=JSON.parse(line);return row.event==='query_capacity'?[row]:[]}catch{return[]}});
 const released=trace.filter(row=>row.operation===6);assert.equal(released.length,10);assert.ok(released.every(row=>row.status===200&&row.snapshots===0));assert.ok(trace.every(row=>row.status===200&&row.snapshots<=1));
 await fs.writeFile(path.join(work,'result.json'),JSON.stringify({passed:true,actualBrowser:true,nativeRns:true,nativeSqlite:true,capacity:1,records:21,automaticRefreshes:8,viewportReplacement:true,emptyViewport:true,morePreserved:true,releases:released.length,physicalUsb:false,publicNetwork:false,trace},null,2));
 console.log('Actual browser/native query leases: capacity 1, 21 records, 8 partial refreshes, viewport replacement, empty view, preserved more and 10 authenticated releases passed.');
}finally{
 await browser?.close();if(service?.child.exitCode===null){await fs.writeFile(path.join(state,'stop'),'stop');await Promise.race([service.done,sleep(10000)]);}
 for(const entry of children){if(entry.child.exitCode===null)entry.child.kill();await entry.done;await fs.writeFile(path.join(work,entry.label+'.log'),entry.output);}
 await new Promise(resolve=>server.close(resolve));assert.equal(path.dirname(state),work);await fs.rm(state,{recursive:true,force:true});
}
