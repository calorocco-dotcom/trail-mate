// Explicit read-only live acceptance; never submits publications or USB commands.
// node tools/tracks/browser_network_probe.mjs ENDPOINT PAGE_URL [tracks|geocaching] [SECONDS] [SEED ...]
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import {chromium} from '../../site/node_modules/playwright/index.mjs';
const config=JSON.parse(await fs.readFile(new URL('../../site/geocaching/network.json',import.meta.url),'utf8'));
const endpoint=process.argv[2]??config.endpoint,pageUrl=process.argv[3],kind=process.argv[4]??'tracks',seconds=Number(process.argv[5]??75);
if(!['tracks','geocaching'].includes(kind)||!Number.isInteger(seconds)||seconds<5||seconds>180)throw Error('Use tracks|geocaching and an observation interval of 5..180 seconds');
const seeds=process.argv.length>6?process.argv.slice(6):(kind==='geocaching'?config.discoverySeeds??[]:config.trackDiscoverySeeds??[]);
if(!Array.isArray(seeds)||seeds.length>3||seeds.some(seed=>typeof seed!=='string'||!/^[0-9a-f]{32}$/i.test(seed)))throw Error('Invalid protocol-specific discovery seeds');
const root=path.resolve(process.env.TRAIL_MATE_LIVE_PROBE_WORK??'.codex-build/hostlink-codec/live-track-network');
await fs.mkdir(root,{recursive:true});
await fs.writeFile(path.join(root,'NOTES.txt'),'Explicit read-only live browser WSS/Reticulum acceptance for Trail Mate. Current production clients, protocol-specific hints, bounded discovery, authenticated directory query and author signature read. No publication or USB commands. Reusable build; remove after verification.');
const source=`import{TrackDirectoryClient}from'./tracks/src/protocol-client.js';
import{DirectoryClient}from'./geocaching/src/protocol-client.js';
import{toHex}from'@reticulum/core';
import{trackShareLink}from'./tracks/src/shared-id.js';
window.startProbe=async({endpoint,kind,seeds,seconds})=>{
 const events=[],started=performance.now();
 const client=new (kind==='tracks'?TrackDirectoryClient:DirectoryClient)(event=>{
  if(events.length<100)events.push({atMs:Math.round(performance.now()-started),type:event.type,state:event.state??null,message:event.message??null});
 });
 const result={protocol:client.appType,seeds:seeds.slice(),online:false,discoveryComplete:false,directories:[],query:null};
 // This observation limit closes requests and reconnect, not a build or service.
 let expired=false;
 const timer=setTimeout(()=>{expired=true;client.close().catch(()=>{});},seconds*1000);
 try{
  await client.connect(endpoint,null,seeds);result.online=client.interface.online;
  while(!expired&&!client.closed){
   const directories=await client.verifiedDirectories();
   if(directories.length){
    result.directories=directories.map(row=>({key:row.key,name:row.name,delivery:toHex(row.delivery),publicKey:toHex(row.publicKey),epoch:toHex(row.epoch),capabilitiesVerified:row.capabilitiesVerified,receiptSupported:kind==='geocaching'?row.receiptSupported===true:null,operations:client.directories.get(row.key)?.operations??null}));
    result.discoveryComplete=true;break;
   }
   await new Promise(resolve=>setTimeout(resolve,250));
  }
  if(!result.discoveryComplete)throw Error('No capability-verified '+kind+' directory observed within the deadline');
  if(kind==='tracks'){
   const directory=result.directories[0];
   const page=await client.queryPage(directory.key,{box:[-1800000000,-900000000,1799999999,900000000],limit:1});
   result.query={authenticated:true,count:page.items.length,snapshot:String(page.snapshot),more:page.cursor!==null};
   if(page.items.length){
    const item=page.items[0],read=await client.readTrack(directory.key,trackShareLink(location.origin+'/trail-mate/',item.objectId,{revision:item.revisionHash}),{download:false});
    result.query.first={objectId:toHex(read.objectId),revisionHash:toHex(read.revisionHash),current:read.current,authorSignatureVerified:true};
   }
  }else{
   await client.query([-90,-180,90,180]);
   const rows=[...client.rows.values()];
   result.query={authenticated:true,count:rows.length,more:client.pages.length>0};
   if(rows.length){
    const row=rows[0],read=await client.get(row.id);
    result.query.first={objectId:row.id,current:read.isCurrent,authorSignatureVerified:true};
    const source=client.directories.get(row.source);
    if(source?.receiptSupported===true){await client.publicationReceipt(row.source,read.signed,read.record[1]);result.query.first.directoryProofVerified=true;}
   }
  }
 }catch(error){result.error=String(error);result.expired=expired;}
 finally{clearTimeout(timer);await client.close().catch(error=>{result.closeError=String(error)});result.closed=client.closed;result.onlineAfterClose=client.interface?.online??false;result.elapsedMs=Math.round(performance.now()-started);result.events=events;}
 return result;
};`;
await build({stdin:{contents:source,loader:'js',resolveDir:path.resolve('site')},bundle:true,format:'esm',outfile:path.join(root,'probe.js'),logLevel:'silent'});
const server=http.createServer(async(req,res)=>{if(req.url==='/probe.js'){res.setHeader('Content-Type','text/javascript');res.end(await fs.readFile(path.join(root,'probe.js')));}else{res.setHeader('Content-Type','text/html');res.end('<!doctype html><script type="module" src="/probe.js"></script>');}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));let browser;
const diagnostics=[];
try{
 browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
 const page=await browser.newPage();
 page.on('console',message=>{if(message.type()==='error'&&diagnostics.length<20)diagnostics.push(message.text());});
 page.on('pageerror',error=>{if(diagnostics.length<20)diagnostics.push(String(error));});
 await page.goto(pageUrl??'http://127.0.0.1:'+server.address().port);
 if(pageUrl)await page.addScriptTag({type:'module',content:await fs.readFile(path.join(root,'probe.js'),'utf8')});
 await page.waitForFunction(()=>typeof window.startProbe==='function');
 const result=await Promise.race([
  page.evaluate(options=>window.startProbe(options),{endpoint,kind,seeds,seconds}),
  new Promise((_,reject)=>{const timer=setTimeout(()=>reject(Error('Live probe did not close after its observation deadline')),(seconds+15)*1000);timer.unref();})
 ]);
 const report={endpoint,pageUrl:page.url(),kind,...result,diagnostics};
 await fs.writeFile(path.join(root,'result-'+kind+'.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
 if(result.error||!result.online||!result.discoveryComplete||!result.query?.authenticated||!result.closed||result.onlineAfterClose||result.closeError)process.exitCode=1;
}catch(error){
 const report={endpoint,pageUrl,kind,error:String(error),diagnostics};
 await fs.writeFile(path.join(root,'result-'+kind+'.json'),JSON.stringify(report,null,2));console.error(JSON.stringify(report));process.exitCode=1;
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
