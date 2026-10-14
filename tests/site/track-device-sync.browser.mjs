import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {trackDeviceSyncText} from '../../site/i18n/track-device-sync-labels.js';
import {trackDeviceRestoreText} from '../../site/i18n/track-device-restore-labels.js';
import {locales} from '../../site/i18n/registry.js';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv',process.platform==='win32'?'Scripts/python.exe':'bin/python');
const vector=JSON.parse(execFileSync(python,['-m','tools.tracks.receipt_vector'],{encoding:'utf8'})),bin=value=>new Uint8Array(Buffer.from(value,'hex'));
const signed=vector.manifest.map(bin),fields=decode(signed[0]),proof=vector.receipt.map(bin),receiptFields=decode(proof[0]);
// USB proof uses an independent real identity; its signed manifest/receipt are
// generated below so every browser cryptographic check remains active.
const account=await Identity.generate(),directory=await Identity.generate(),key=await account.getPublicKey(),directoryKey=await directory.getPublicKey();
const delivery=async identity=>Identity.truncatedHash(new Uint8Array([...((await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10)),...identity.identityHash]));
const authorDelivery=await delivery(account),directoryDelivery=await delivery(directory),sessionId=new Uint8Array(16).fill(1),epoch=new Uint8Array(16).fill(2),requestId=new Uint8Array(16).fill(3);
fields[1]=key;const manifest=encode(fields);
const {trackManifestSigningBytes,verifyTrackManifest}=await import('../../site/tracks/src/manifest.js');
const signature=await account.sign(trackManifestSigningBytes(manifest)),verified=await verifyTrackManifest([manifest,signature]);
const receiptRaw=encode([1,requestId,verified.objectId,verified.revisionHash,fields[4],fields[6],fields[13][0],fields[13][1],directoryKey,directoryDelivery,epoch,1,1800000000]);
const receiptSignature=await directory.sign(new Uint8Array([...new TextEncoder().encode('TrailMate.Track.Receipt.v1\0'),...receiptRaw]));
const record={stage:'confirmed',draftId:'browser-sync',draftVersion:1,authorKey:[...key],requestId:[...requestId],directoryKey:[...directoryKey],delivery:[...directoryDelivery],epoch:[...epoch],signature:[...signature],receipt:[...encode([receiptRaw,receiptSignature])],
 plan:{operationId:Array(16).fill(23),kmlUploadId:Array(16).fill(24),manifestUploadId:Array(16).fill(25),generation:7,manifest:[...manifest],fileHash:[...fields[13][0]],fileBytes:fields[13][1]}};
const root=path.resolve('.codex-build/site-track-device-sync');await fs.mkdir(root,{recursive:true});
await fs.writeFile(path.join(root,'NOTES.txt'),'Purpose: browser device receipt synchronization UI test. Origin: web-device-outdoor-management, 2026-10-12. Cleanup: remove generated bundle when acceptance completes. No private device identity stored.\n');
await build({stdin:{contents:`import React,{useState} from 'react';import{createRoot}from'react-dom/client';import{TrackDeviceSync}from'./tracks/src/track-device-sync.jsx';import{ManagementCopyProvider}from'./i18n/management-copy.jsx';import{saveTrackIPCheckpoint}from'./tracks/src/ip-publication-journal.js';
const r=window.fixture.record;for(const key of ['authorKey','requestId','directoryKey','delivery','epoch','signature','receipt'])r[key]=Uint8Array.from(r[key]);for(const key of ['operationId','kmlUploadId','manifestUploadId','manifest','fileHash'])r.plan[key]=Uint8Array.from(r.plan[key]);
try{saveTrackIPCheckpoint(r);}catch{}const session={id:Uint8Array.from(window.fixture.session),request:async(op,body)=>window.normalize(await window.device(op,body.map(value=>value instanceof Uint8Array?[...value]:value)))};
function App(){const[locale,setLocale]=useState('zh-Hans');window.language=setLocale;return <ManagementCopyProvider locale={locale}><TrackDeviceSync record={r} session={session} operations={[1,2,32,33,35,37,97,98]}/></ManagementCopyProvider>;}createRoot(document.getElementById('root')).render(<App/>);`,resolveDir:path.resolve('site'),loader:'jsx'},bundle:true,outfile:path.join(root,'page.js'),format:'esm',loader:{'.png':'dataurl','.svg':'dataurl','.woff2':'dataurl','.woff':'dataurl','.ttf':'dataurl'},logLevel:'silent'});
const server=http.createServer(async(req,res)=>{if(req.url==='/page.js'){res.setHeader('Content-Type','text/javascript');res.end(await fs.readFile(path.join(root,'page.js')));return;}res.end('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><div id="root"></div><script type="module" src="page.js"></script>');});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
try{const page=await browser.newPage(),errors=[],calls=[];let transfer,total,offset,kind,lose=true,loseImport=true,generation=7;
 page.on('pageerror',error=>errors.push(String(error)));await page.addInitScript(fixture=>{window.fixture=fixture;},{record,session:[...sessionId]});
 await page.exposeFunction('device',async(op,body)=>{calls.push({op,body});
  if(op===1)return[[...key],[...account.identityHash],[...authorDelivery],generation];
  if(op===2)return[[...key],[...account.identityHash],[...authorDelivery],generation,[...await account.sign(authorProofMessage(sessionId,Uint8Array.from(body[0]),key,generation))]];
  if(op===32){[transfer,kind,total]=body;offset=kind===7?0:total;return[transfer,320,offset,kind===7?0:1];}
  if(op===33){offset+=body[2].length;return[transfer,320,offset,0];}if(op===35)return[transfer,320,total,1];
  if(op===97){if(lose){lose=false;throw Error('Lost USB reply');}return[body[0],body[1],7,1,[...verified.objectId],[...verified.revisionHash],fields[4],1];}
  if(op===98){assert.equal(body.length,14);assert.equal(body[3],8);if(loseImport){loseImport=false;throw Error('Lost import reply');}return[body[0],body[1],8,1,[...verified.objectId],[...verified.revisionHash],fields[4],1];}
  if(op===37){assert.equal(body.length,4);return[];}throw Error('Unexpected operation');
 });
 // Exposed functions serialize typed arrays as ordinary arrays; normalize at
 // the page boundary, exactly as the production management decoder does.
 await page.addInitScript(()=>{window.normalize=value=>Array.isArray(value)?value.map(item=>Array.isArray(item)&&item.every(v=>Number.isInteger(v)&&v>=0&&v<=255)?Uint8Array.from(item):item):value;});
 await page.goto('http://127.0.0.1:'+server.address().port);
 const text=trackDeviceSyncText('zh-Hans');await page.getByRole('button',{name:text.sync,exact:true}).waitFor();
 for(const locale of locales){await page.evaluate(id=>window.language(id),locale.id);await page.getByRole('button',{name:trackDeviceSyncText(locale.id).sync,exact:true}).waitFor();await page.getByRole('button',{name:trackDeviceRestoreText(locale.id).restore,exact:true}).waitFor();}
 await page.evaluate(()=>window.language('zh-Hans'));await page.getByRole('button',{name:text.sync,exact:true}).click();await page.getByRole('alert').waitFor();
 await page.reload();await page.getByRole('button',{name:text.resume,exact:true}).click();await page.getByText(text.complete,{exact:true}).waitFor();
 assert.equal(calls.filter(call=>call.op===97).length,2);assert.deepEqual(calls.filter(call=>call.op===97).map(call=>call.body)[0],calls.filter(call=>call.op===97).map(call=>call.body)[1]);
 assert.equal(calls.filter(call=>call.op===37).length,3);assert.ok(calls.every(call=>[1,2,32,33,35,37,97].includes(call.op)));
 generation=8;const restore=trackDeviceRestoreText('zh-Hans');
 await page.getByRole('button',{name:restore.restore,exact:true}).click();await page.getByRole('alert').waitFor();
 await page.reload();await page.getByRole('button',{name:restore.restore,exact:true}).click();
 await page.waitForFunction(label=>{const button=[...document.querySelectorAll('button')].find(item=>item.textContent===label);return button&&!button.disabled&&!document.querySelector('[role="alert"]');},restore.restore);
 const imports=calls.filter(call=>call.op===98);assert.equal(imports.length,2);assert.deepEqual(imports[0].body,imports[1].body);
 const restoredCleanup=calls.filter(call=>call.op===37).slice(3);assert.equal(restoredCleanup.length,2);assert.deepEqual(restoredCleanup.map(call=>call.body[1]),[4,7]);
 assert.deepEqual(restoredCleanup.map(call=>call.body[0]),[imports[1].body[11],imports[1].body[1]]);
 assert.ok(calls.every(call=>[1,2,32,33,35,37,97,98].includes(call.op)));
 await page.setViewportSize({width:390,height:844});assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));assert.deepEqual(errors,[]);
 console.log('Track device sync/history restore UI: 11 locales, real proofs/receipts, changed generation, lost replies, reload with fixed transfers, exact cleanup and mobile passed.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
