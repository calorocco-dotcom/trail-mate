// Operator opens this URL in their ordinary visible browser. No browser is
// launched by this process. The current device page is gated to read-only USB.
import http from 'node:http';import path from 'node:path';import fs from 'node:fs/promises';import {build} from '../../site/node_modules/esbuild/lib/main.js';
const root=path.resolve('site'),resultPath=path.resolve('.codex-build/hostlink-wio-l2/web-serial-read.json');
const hook=await build({entryPoints:['tools/hostlink/web_serial_observer.js'],bundle:true,write:false,format:'iife',platform:'browser'});
let lastReport;
const server=http.createServer(async(req,res)=>{
 try{
  const pathname=new URL(req.url,'http://localhost').pathname;
  if(pathname==='/__observer.js'){res.setHeader('Content-Type','text/javascript');res.end(hook.outputFiles[0].text);return;}
  if(pathname==='/__status'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(lastReport??{waiting:true}));return;}
  if(pathname==='/__report'&&req.method==='POST'){
   if(req.headers.origin!==origin){res.writeHead(403).end();return;}
   const parts=[];let size=0;for await(const part of req){size+=part.length;if(size>16384){res.writeHead(413).end();return;}parts.push(part);}
   const row=JSON.parse(Buffer.concat(parts).toString('utf8'));
   // Explicit allowlist: even a modified browser report cannot persist a full
   // configuration, arbitrary field values, keys or other supplied properties.
   if(!Number.isSafeInteger(row.opened)||!Number.isSafeInteger(row.closed)||!Number.isSafeInteger(row.snapshots)||!Array.isArray(row.operations)||row.operations.length>512||row.operations.some(op=>![0,1,2,3,7,17].includes(op)))throw Error('Report shape');
   const config=row.config;if(config&&(!/^[a-f0-9]{64}$/.test(config.sha256)||!Number.isSafeInteger(config.fields)||!Number.isSafeInteger(config.bytes)))throw Error('Report config');
   lastReport={nativeSerial:true,opened:row.opened,closed:row.closed,snapshots:row.snapshots,capabilityCount:row.capabilityCount??null,operations:row.operations.slice(),config:config?{sha256:config.sha256,fields:config.fields,bytes:config.bytes}:null,authorVerified:row.authorVerified===true,configurationWrites:0,identityExports:0};
   await fs.writeFile(resultPath,JSON.stringify(lastReport,null,2));console.log(JSON.stringify(lastReport));res.end('ok');return;
  }
  const name=decodeURIComponent(pathname);if(!name.startsWith('/trail-mate/'))throw Error('prefix');const file=path.resolve(root,name.slice(12)+(name.endsWith('/')?'index.html':''));if(!file.startsWith(root+path.sep))throw Error('path');
  let bytes=await fs.readFile(file);
  if(file===path.join(root,'device','index.html')){
   const banner='<aside style="padding:12px;background:#fff4d7;border:2px solid #ab762d">实机只读验收页：点击“连接设备”，选择 COM69；读完后断开，再连接一次。所有配置写入及密钥导出／恢复命令均已拦截。测试状态：<span id="probe-state">等待连接</span></aside>';
   const reporter='<script>let last="";setInterval(()=>{if(!window.webSerialProbeSummary)return;const value=webSerialProbeSummary();value.authorVerified=document.querySelector("[data-author-verified]")?.dataset.authorVerified==="true";const data=JSON.stringify(value);document.getElementById("probe-state").textContent=value.snapshots+" 次完整读取 / "+value.closed+" 次断开";if(data!==last){last=data;fetch("/__report",{method:"POST",headers:{"Content-Type":"application/json"},body:data}).catch(()=>{});}},1000);</script>';
   bytes=Buffer.from(bytes.toString('utf8').replace('<head>','<head><script src="/__observer.js"></script>').replace('<body>','<body>'+banner+reporter));
  }
  res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(file)]??'application/octet-stream');res.end(bytes);
 }catch{res.writeHead(404).end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
console.log(JSON.stringify({event:'device_test_ready',url:origin+'/trail-mate/device/?lang=zh-Hans',status:origin+'/__status',durationMinutes:30}));
const end=setTimeout(()=>server.close(),30*60*1000);end.unref();process.on('SIGINT',()=>server.close());process.on('SIGTERM',()=>server.close());
