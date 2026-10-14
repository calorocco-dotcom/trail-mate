import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {execFileSync} from 'node:child_process';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import {build} from '../../site/node_modules/esbuild/lib/main.js';

// Requires the optional native real-Mbed-TLS verification target. Missing
// helper is a failure, never silently replaced by a dispatch/fake hash.
const executable=path.resolve('.codex-build/hostlink-codec/native/snapshot_stream_hash'+(process.platform==='win32'?'.exe':''));
const native=execFileSync(executable,{encoding:'utf8'}).trim();assert.match(native,/^[0-9a-f]{64}$/);
const copyExecutable=path.resolve('.codex-build/hostlink-codec/native/snapshot_files_sha256'+(process.platform==='win32'?'.exe':''));
const lines=execFileSync(copyExecutable,{encoding:'utf8'}).trim().split(/\r?\n/),copiedHashes=lines.filter(line=>/^[0-9a-f]{64}$/.test(line)),chunks=lines.filter(line=>line.startsWith('chunk:')).map(line=>line.slice(6));assert.equal(copiedHashes.length,2);assert.equal(chunks.length,3);
const bundle=await build({stdin:{contents:'export {readDeviceTrackSnapshot} from "./site/tracks/src/device-snapshot.js"; export {decode} from "./site/geocaching/src/protocol.js";',resolveDir:process.cwd(),sourcefile:'snapshot-client.js'},bundle:true,platform:'browser',format:'esm',write:false});
const server=http.createServer((request,response)=>{if(request.url==='/snapshot-client.js'){response.setHeader('Content-Type','text/javascript');response.end(bundle.outputFiles[0].text);}else{response.setHeader('Content-Type','text/html');response.end('<!doctype html><title>Snapshot SHA-256</title>');}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try{
 browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});const page=await browser.newPage();
 await page.goto(`http://127.0.0.1:${server.address().port}/`);
 const hash=await page.evaluate(async()=>{const data=new Uint8Array(1000000).fill(97);return [...new Uint8Array(await crypto.subtle.digest('SHA-256',data))].map(byte=>byte.toString(16).padStart(2,'0')).join('');});
 assert.equal(hash,native);assert.equal(hash,'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0');
 const fileHash=await page.evaluate(async()=>{const data=Uint8Array.from({length:750},(_,index)=>index%251);return [...new Uint8Array(await crypto.subtle.digest('SHA-256',data))].map(byte=>byte.toString(16).padStart(2,'0')).join('');});
 assert.equal(fileHash,copiedHashes[0],'Actual file copy SHA disagrees with browser digest');
 const maximumHash=await page.evaluate(async()=>{const data=Uint8Array.from({length:8*1024*1024},(_,index)=>index%251);return [...new Uint8Array(await crypto.subtle.digest('SHA-256',data))].map(byte=>byte.toString(16).padStart(2,'0')).join('');});
 assert.equal(maximumHash,copiedHashes[1],'8 MiB file copy SHA disagrees with browser digest');
 const snapshot=await page.evaluate(async chunks=>{
  const {readDeviceTrackSnapshot,decode}=await import('/snapshot-client.js'),offsets=[];
  const id=new Uint8Array(16);id[0]=1;
  const session={async request(operation,body){if(operation!==81)throw Error('Wrong operation');const chunk=chunks[offsets.length];offsets.push(body[1]);if(!chunk)throw Error('Unexpected request');return decode(Uint8Array.from(chunk.match(/../g),value=>parseInt(value,16)),512);}};
  const result=await readDeviceTrackSnapshot(session,id);
  return {source:[...result.source],hash:[...result.sha256].map(byte=>byte.toString(16).padStart(2,'0')).join(''),format:result.format,offsets};
 },chunks);
 assert.deepEqual(snapshot.offsets,[0,320,640]);assert.equal(snapshot.format,3);assert.equal(snapshot.hash,copiedHashes[0]);assert.equal(snapshot.source.length,750);assert.ok(snapshot.source.every((byte,index)=>byte===index%251));
 console.log('Real SDK Mbed TLS file SHA and C++ container chunk responses interoperate with actual browser snapshot reader, including 8 MiB hash verification.');
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
