import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {locales} from '../../site/i18n/registry.js';
import {geocachingDraftText} from '../../site/i18n/geocaching-draft-labels.js';
import {geocachingPublicationText} from '../../site/i18n/geocaching-publication-labels.js';
import {geocachingMaintenanceText} from '../../site/i18n/geocaching-maintenance-labels.js';
import {geocachingMaintenanceStatusText} from '../../site/i18n/geocaching-maintenance-status.js';
import {geocachingDraftActionText} from '../../site/i18n/geocaching-draft-action-labels.js';
import {geocachingTransferText} from '../../site/i18n/geocaching-transfer-labels.js';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {FrameDecoder,encodeFrame,FRAME} from '../../site/device/src/hostlink.js';
import {decodeManagement,encodeManagement,OPERATION} from '../../site/device/src/management-protocol.js';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {encode,decode} from '../../site/geocaching/src/protocol.js';
import {savePublicationCheckpoint} from '../../site/geocaching/src/publication-journal.js';
const root=path.resolve('site');
const server=http.createServer(async(req,res)=>{try{
  const pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  if(!pathname.startsWith('/trail-mate/'))throw Error('prefix');
  const filename=path.resolve(root,pathname.slice(12)+(pathname.endsWith('/')?'index.html':''));
  if(!filename.startsWith(root+path.sep))throw Error('path');
  const contents=await fs.readFile(filename);
  res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(filename)]??'application/octet-stream');res.end(contents);
}catch{res.writeHead(404).end();}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const origin=`http://127.0.0.1:${server.address().port}`;
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
const page=await browser.newPage({viewport:{width:1440,height:960},locale:'zh-CN'});
const errors=[];page.on('pageerror',e=>errors.push(String(e)));const calls=[];
const identity=await Identity.generate(),key=await identity.getPublicKey();
const nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
const destination=await Identity.truncatedHash(Uint8Array.from([...nameHash,...identity.identityHash]));
const info=[key,identity.identityHash,destination,7],draftId=new Uint8Array(16).fill(1);
const directoryIdentity=await Identity.generate(),directoryKey=await directoryIdentity.getPublicKey();
const directoryDelivery=await Identity.truncatedHash(Uint8Array.from([...nameHash,...directoryIdentity.identityHash]));
const directoryAspect=(await Identity.fullHash(new TextEncoder().encode('trailmate.geocache.directory'))).slice(0,10);
const directoryDiscovery=await Identity.truncatedHash(Uint8Array.from([...directoryAspect,...directoryIdentity.identityHash]));
let directoryReads=0;
let activeIdentity=identity,activeInfo=info;
let draftValue=[key,null,[0,310000000,1210000000,'USB 草稿','<script>不执行</script>\n设备中的正文','提示',3,4,2],1],draft=encode(draftValue);
const idText=id=>Buffer.from(id).toString('hex'),records=new Map([[idText(draftId),{id:draftId,value:draftValue}]]);
let uploadId,uploadOwner,uploadTotal,uploaded=[],sealed=false,commits=0,commitBusy=true,failNextChunk=false,resumed=false,uploadExpected,uploadTarget,savedReceipt,catalogEpoch=0;
let publicationPrepared,publicationTask,publicationCommits=0,publicationConfirmed=false,publicationReplyFault=true;
let publicationStopped=false,publicationStopCalls=0;
await page.exposeFunction('deviceReply',async raw=>{
  const frame=new FrameDecoder().push(Uint8Array.from(raw))[0];assert.ok(frame);
  if(frame.type===FRAME.HELLO)return [...encodeFrame(FRAME.HELLO_ACK,frame.sequence,new Uint8Array([1,0,0,2,0,4,0,0,6,...new TextEncoder().encode('T-Deck'),4,...new TextEncoder().encode('test')]))];
  const request=decodeManagement(frame.payload);calls.push(request.code);let body,status=0;
  if(request.code===0)body=[1,512,320,1,[0,1,2,3,32,33,35,36,48,50,64,65,66,67,68,70],7];
  else if(request.code===1)body=activeInfo;
  else if(request.code===2)body=[...activeInfo,await activeIdentity.sign(authorProofMessage(request.session,request.body[0],activeInfo[0],activeInfo[3]))];
  else if(request.code===64){const offset=request.body[0];assert.equal(request.body[2],7);const row=[...records.values()][offset],value=row?.value;
    const publication=publicationTask&&row&&idText(publicationTask.id)===idText(row.id)?[1,publicationConfirmed?1:0,publicationConfirmed?0:1]:[0,0,0];
    body=[catalogEpoch,offset,records.size,row?[row.id,value[3],value[0],value[2][3],value[2][0],value[2][1],value[2][2],value[1],publication]:null];}
  else if(request.code===65){const [id,offset,count]=request.body,row=records.get(idText(id));assert.ok(row);const raw=encode(row.value);body=[row.id,row.value[3],raw.length,offset,raw.slice(offset,offset+count)];}
  else if(request.code===70){const [offset]=request.body;if(!offset)directoryReads++;
    body=[offset,directoryReads===1?0:1,offset||directoryReads===1?null:[directoryDelivery,directoryDiscovery,directoryKey,new Uint8Array(16),1,'测试设备目录',directoryReads===2?0:1,directoryReads===2?0:20,1000]];}
  else if(request.code===67){const [id,generation,directory,authorGeneration,operation]=request.body;assert.equal(authorGeneration,7);
    const row=records.get(idText(id));assert.ok(row);assert.equal(BigInt(generation),BigInt(row.value[3]));assert.deepEqual(directory,directoryDelivery);
    publicationPrepared={id,generation,directory,operation,owner:request.session,hash:await Identity.fullHash(encode(row.value))};
    body=[operation,id,generation,key,directory,new Uint8Array(16),publicationPrepared.hash,0,1,120000];}
  else if(request.code===68){const [operation,hash,generation]=request.body;assert.equal(generation,7);assert.ok(publicationPrepared);
    assert.deepEqual(operation,publicationPrepared.operation);assert.deepEqual(hash,publicationPrepared.hash);assert.deepEqual(request.session,publicationPrepared.owner);
    if(!publicationTask){publicationCommits++;const row=records.get(idText(publicationPrepared.id));row.value[0]=key;row.value[3]++;
      publicationTask={...publicationPrepared,cache:await Identity.fullHash(Uint8Array.from([...new TextEncoder().encode('trailmate.geocache/id/v1\0'),...key,...row.id])),revisionHash:await Identity.fullHash(encode(row.value))};catalogEpoch++;}
    body=[operation,publicationPrepared.id,publicationPrepared.generation,publicationPrepared.directory,hash,1,1];
    if(publicationReplyFault){publicationReplyFault=false;status=11;body=[];}}
  else if(request.code===48||request.code===50){assert.ok(publicationTask);assert.deepEqual(request.body,[publicationTask.operation,7]);assert.deepEqual(request.session,publicationTask.owner);
    if(request.code===50){publicationStopCalls++;if(publicationStopCalls===1)status=11;else if(publicationStopCalls===2)status=5;else publicationStopped=true;}
    body=status?[]:[publicationTask.operation,publicationConfirmed?3:publicationStopped?4:2,publicationTask.cache,publicationTask.revisionHash,1,1,publicationConfirmed?1:null,null];}
  else if(request.code===32){if(uploadId&&Buffer.from(uploadId).equals(Buffer.from(request.body[0]))){assert.deepEqual(request.session,uploadOwner);assert.equal(request.body[2],uploadTotal);resumed=true;}
    else{[uploadId,,uploadTotal]=request.body;uploadOwner=request.session;uploaded=[];sealed=false;uploadExpected=undefined;uploadTarget=undefined;savedReceipt=undefined;}body=[uploadId,320,uploaded.length,sealed?1:0];}
  else if(request.code===33){assert.equal(request.body[1],uploaded.length);uploaded.push(...request.body[2]);body=[uploadId,320,uploaded.length,0];if(failNextChunk){failNextChunk=false;status=11;body=[];}}
  else if(request.code===35){assert.equal(uploaded.length,uploadTotal);sealed=true;body=[uploadId,320,uploaded.length,1];}
  else if(request.code===66){assert.ok(sealed);assert.deepEqual(request.session,uploadOwner);uploadExpected??=request.body[2];uploadTarget??=request.body[1];assert.deepEqual(request.body,[uploadId,uploadTarget,uploadExpected,7]);
    if(commitBusy){commitBusy=false;status=5;body=[];}
    else{if(!savedReceipt){const edit=decode(Uint8Array.from(uploaded),4096);assert.equal(edit[0],null);assert.equal(edit[1],null);
      const previous=records.get(idText(uploadTarget));assert.equal(previous?.value[3]??0,uploadExpected);
      const value=[previous?.value[0]??null,previous?.value[1]??null,edit[2],edit[3]];records.set(idText(uploadTarget),{id:uploadTarget,value});
      if(idText(uploadTarget)===idText(draftId)){draftValue=value;draft=encode(value);}commits++;catalogEpoch++;savedReceipt=[uploadId,uploadTarget,edit[3]];}body=savedReceipt;}}
  else if(request.code===3)body=[];
  else assert.fail('unsupported operation');
  return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,status,body))];
});
await page.addInitScript(()=>{
  window.Worker=class{postMessage(){}terminate(){}};
  let input;const port={async open(){this.readable=new ReadableStream({start(c){input=c;}});this.writable=new WritableStream({async write(bytes){input.enqueue(Uint8Array.from(await window.deviceReply([...bytes])));}});},async setSignals(){},async close(){}};
  Object.defineProperty(navigator,'serial',{configurable:true,value:{async requestPort(){return port;}}});
  Object.defineProperty(navigator,'clipboard',{configurable:true,value:{async writeText(value){window.testCopiedId=value;}}});
});
await page.route('https://**',route=>route.abort());
try{
  await page.goto(origin+'/trail-mate/geocaching/?lang=zh-Hans');
  await page.getByRole('button',{name:'草稿',exact:true}).click();
  assert.equal(await page.locator('#geocaching-browse-controls').isVisible(),false);
  await page.getByRole('button',{name:'连接设备',exact:true}).click();
  await page.getByText('USB 作者已验证',{exact:true}).waitFor();
  assert.equal(await page.getByRole('button',{name:'刷新设备目录',exact:true}).count(),0);
  assert.equal(directoryReads,0,'new web maintenance must not discover RF directories');
  await page.getByRole('button',{name:'刷新设备列表',exact:true}).click();
  await page.locator('.geo-draft-row').waitFor();
  assert.equal(await page.locator('.geo-draft-row').count(),1);
  await page.locator('.geo-draft-row').click();
  await page.locator('.geo-draft-detail').waitFor();
  assert.ok((await page.getByLabel('正文',{exact:true}).inputValue()).includes('<script>不执行</script>'));
  assert.equal(await page.locator('.geo-draft-detail script').count(),0);
  await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-geocaching-drafts.png',fullPage:true});
  await page.setViewportSize({width:390,height:844});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'mobile layout overflow');
  await page.setViewportSize({width:1440,height:960});
  await page.getByLabel('名称',{exact:true}).fill('新名称');
  await page.getByLabel('正文',{exact:true}).fill('中'.repeat(700));
  assert.equal(await page.getByRole('button',{name:'保存草稿到设备',exact:true}).isDisabled(),true);
  const validationCalls=calls.length;
  for(const locale of locales){const text=geocachingDraftText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByText(text.textInvalid,{exact:true}).waitFor();assert.equal(await page.getByRole('button',{name:text.save,exact:true}).isDisabled(),true);assert.equal(await page.getByLabel(text.description,{exact:true}).inputValue(),'中'.repeat(700));assert.equal(calls.length,validationCalls);}
  await page.locator('.management-language select').selectOption('zh-Hans');
  await page.getByLabel('正文',{exact:true}).fill('中'.repeat(500));
  await page.getByLabel('纬度',{exact:true}).fill('');
  assert.equal(await page.getByRole('button',{name:'保存草稿到设备',exact:true}).isDisabled(),true);
  await page.getByLabel('纬度',{exact:true}).fill('31.1234567');
  const fieldValues=await page.locator('.geo-draft-editor input,.geo-draft-editor textarea,.geo-draft-editor select').evaluateAll(nodes=>nodes.map(node=>node.value)),editCalls=calls.length,editCommits=commits;
  for(const locale of locales){
    const text=geocachingMaintenanceText(locale.id),action=geocachingDraftActionText(locale.id);
    await page.locator('.management-language select').selectOption(locale.id);
    await page.getByRole('button',{name:text.close,exact:true}).waitFor();
    for(const [button,message] of [[page.getByRole('button',{name:text.close,exact:true}),action.discardClose],[page.getByRole('button',{name:text.new,exact:true}),action.discardNew],[page.locator('.geo-draft-row').first(),action.discardOpen],[page.getByRole('button',{name:text.refresh,exact:true}),geocachingTransferText(locale.id).discardRefresh]]){
      const dialogPromise=page.waitForEvent('dialog');const clicking=button.click();const dialog=await dialogPromise;
      assert.equal(dialog.message(),message);await dialog.dismiss();await clicking;
      assert.deepEqual(await page.locator('.geo-draft-editor input,.geo-draft-editor textarea,.geo-draft-editor select').evaluateAll(nodes=>nodes.map(node=>node.value)),fieldValues);
      assert.equal(calls.length,editCalls);assert.equal(commits,editCommits);
    }
  }
  for(const locale of locales){const text=geocachingDraftText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByRole('button',{name:text.save,exact:true}).waitFor();assert.deepEqual(await page.locator('.geo-draft-editor input,.geo-draft-editor textarea,.geo-draft-editor select').evaluateAll(nodes=>nodes.map(node=>node.value)),fieldValues);assert.deepEqual(await page.getByLabel(text.state,{exact:true}).locator('option').evaluateAll(nodes=>nodes.map(node=>node.value)),['0','1','2']);assert.equal(await page.getByLabel(text.latitude,{exact:true}).getAttribute('dir'),'ltr');assert.equal(calls.length,editCalls);assert.equal(commits,editCommits);}
  await page.locator('.management-language select').selectOption('zh-Hans');
  await page.getByRole('button',{name:'保存草稿到设备',exact:true}).click();
  await page.getByText('草稿已保存到设备。刷新列表可更新发布状态。',{exact:true}).waitFor();
  assert.equal(commits,1);assert.equal(draftValue[3],2);assert.equal(draftValue[2][1],311234567);assert.equal(draftValue[2][3],'新名称');
  assert.equal(await page.getByLabel('名称',{exact:true}).inputValue(),'新名称');
  await page.locator('.geo-draft-detail').evaluate(element=>element.scrollTop=0);
  await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-geocaching-editor.png',fullPage:true});
  await page.getByLabel('名称',{exact:true}).fill('断线恢复草稿');failNextChunk=true;
  await page.getByRole('button',{name:'保存草稿到设备',exact:true}).click();
  await page.getByText(/保存未确认：/).waitFor();
  assert.equal(await page.getByLabel('名称',{exact:true}).isDisabled(),true);
  const pendingCalls=calls.length,pendingCommits=commits;
  for(const locale of locales){const text=geocachingDraftText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByText(text.unconfirmed,{exact:true}).waitFor();await page.getByText(geocachingTransferText(locale.id).saveUnconfirmed,{exact:false}).waitFor();assert.equal(await page.getByLabel(text.name,{exact:true}).inputValue(),'断线恢复草稿');assert.equal(await page.getByLabel(text.name,{exact:true}).isDisabled(),true);assert.equal(calls.length,pendingCalls);assert.equal(commits,pendingCommits);}
  await page.locator('.management-language select').selectOption('zh-Hans');
  await page.getByRole('button',{name:'断开 USB',exact:true}).click();
  assert.equal(await page.getByLabel('名称',{exact:true}).inputValue(),'断线恢复草稿');
  await page.reload();
  await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('USB 作者已验证',{exact:true}).waitFor();
  await page.getByText('已恢复待确认的草稿保存。请先查询结果；重试会使用原内容和原上传 ID。',{exact:true}).waitFor();
  assert.equal(await page.getByLabel('名称',{exact:true}).inputValue(),'断线恢复草稿');
  const beforeQuery=calls.length;
  await page.getByRole('button',{name:'查询保存结果',exact:true}).click();
  await page.getByText('设备上的内容尚未匹配这次保存。保留原操作，可重试或核对设备。',{exact:true}).waitFor();
  assert.ok(calls.slice(beforeQuery).every(code=>[65,3].includes(code)));assert.equal(commits,1);
  await page.getByRole('button',{name:'重试并确认保存',exact:true}).click();
  await page.getByText('草稿已保存到设备。刷新列表可更新发布状态。',{exact:true}).waitFor();
  assert.equal(resumed,true);assert.equal(commits,2);assert.equal(draftValue[3],3);assert.equal(draftValue[2][3],'断线恢复草稿');
  await page.getByRole('button',{name:'新建草稿',exact:true}).click();
  assert.equal(await page.getByLabel('名称',{exact:true}).inputValue(),'');
  await page.getByLabel('名称',{exact:true}).fill('网页创建草稿');
  await page.getByLabel('正文',{exact:true}).fill('从步道入口沿指示牌前进。\n请保持现场整洁。');
  await page.getByLabel('提示',{exact:true}).fill('观察步道旁的石墙。');
  await page.getByRole('button',{name:'在地图上选择坐标',exact:true}).click();
  await page.locator('#map').click({position:{x:220,y:300}});
  await page.getByRole('button',{name:'在地图上选择坐标',exact:true}).waitFor();
  const latitude=Number(await page.getByLabel('纬度',{exact:true}).inputValue()),longitude=Number(await page.getByLabel('经度',{exact:true}).inputValue());
  assert.ok(Number.isFinite(latitude)&&Math.abs(latitude)<=90&&Number.isFinite(longitude)&&longitude>=-180&&longitude<180);
  await page.locator('.geo-draft-marker').waitFor();
  await page.getByRole('button',{name:'保存草稿到设备',exact:true}).click();await page.getByText('草稿已保存到设备。刷新列表可更新发布状态。',{exact:true}).waitFor();
  const created=[...records.values()].find(row=>row.value[2][3]==='网页创建草稿');assert.ok(created);assert.notEqual(idText(created.id),idText(draftId));assert.equal(created.value[3],1);assert.equal(created.value[0],null);assert.equal(commits,3);
  await page.waitForFunction(()=>Object.keys(localStorage).every(name=>!name.startsWith('trailmate.geo.draft-save.v1.')));
  assert.equal(created.value[2][1],Math.round(latitude*1e7));assert.equal(created.value[2][2],Math.round(longitude*1e7));
  const maintenanceCalls=calls.length;
  const maintenanceFields=await page.locator('.geo-draft-editor input,.geo-draft-editor textarea,.geo-draft-editor select').evaluateAll(nodes=>nodes.map(node=>node.value));
  for(const locale of locales){
    const text=geocachingMaintenanceText(locale.id);
    await page.locator('.management-language select').selectOption(locale.id);
    await page.getByRole('navigation',{name:text.modes,exact:true}).waitFor();
    assert.equal(await page.getByRole('button',{name:text.drafts,exact:true}).getAttribute('aria-pressed'),'true');
    await page.getByRole('button',{name:text.close,exact:true}).waitFor();
    await page.getByRole('button',{name:text.chooseCoordinate,exact:true}).waitFor();
    assert.equal(directoryReads,0,'IP directory controls must not request device RF discovery');
    assert.deepEqual(await page.locator('.geo-draft-editor input,.geo-draft-editor textarea,.geo-draft-editor select').evaluateAll(nodes=>nodes.map(node=>node.value)),maintenanceFields);
    assert.equal(calls.length,maintenanceCalls,'language change must not issue a device request');
  }
  await page.locator('.management-language select').selectOption('zh-Hans');
  await page.locator('.geo-draft-detail').evaluate(element=>element.scrollTop=0);
  await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-geocaching-create.png',fullPage:true});
  // New publication uses browser IP. Keep the real-page legacy recovery checks
  // by loading an existing task, rather than resurrecting its removed RF entry.
  assert.equal(publicationCommits,0);assert.equal(directoryReads,0);
  // An existing task uses the owner's actual retained USB session. A
  // fabricated different owner would correctly conflict with its saved account.
  const operation=new Uint8Array(16).fill(6),legacyOwner=uploadOwner.slice();
  const priorGeneration=created.value[3],contentHash=await Identity.fullHash(encode(created.value));
  publicationPrepared={id:created.id,generation:BigInt(priorGeneration),directory:directoryDelivery,operation,owner:legacyOwner,hash:contentHash};
  created.value[0]=key;created.value[3]++;publicationCommits=1;catalogEpoch++;
  publicationTask={...publicationPrepared,cache:await Identity.fullHash(Uint8Array.from([...new TextEncoder().encode('trailmate.geocache/id/v1\0'),...key,...created.id])),revisionHash:await Identity.fullHash(encode(created.value))};
  let checkpointKey,checkpointValue;
  savePublicationCheckpoint({stage:'unknown',title:'网页创建草稿',owner:legacyOwner,prepared:{prepareId:operation,id:created.id,generation:BigInt(priorGeneration),authorKey:key,authorGeneration:7,directory:directoryDelivery,directoryEpoch:new Uint8Array(16),contentHash,fromRevision:0,toRevision:1}},
    {setItem:(name,value)=>{checkpointKey=name;checkpointValue=value;}});
  await page.evaluate(({name,value})=>localStorage.setItem(name,value),{name:checkpointKey,value:checkpointValue});
  await page.evaluate(()=>localStorage.setItem('trailmate.geo.usb-owner.v1','09'.repeat(16)));
  await page.reload();await page.getByRole('button',{name:'草稿',exact:true}).click();
  await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('USB 作者已验证',{exact:true}).waitFor();
  await page.getByText('浏览器上次记录：结果未知',{exact:true}).waitFor();
  await page.getByRole('button',{name:'查询原任务',exact:true}).click();await page.getByText('等待目录结果',{exact:true}).waitFor();
  const taskCalls=calls.length,taskCommits=publicationCommits;
  for(const locale of locales){const text=geocachingPublicationText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByText(text.admitted,{exact:true}).waitFor();assert.equal(calls.length,taskCalls);assert.equal(publicationCommits,taskCommits);}
  await page.locator('.management-language select').selectOption('zh-Hans');
  assert.equal(publicationCommits,1);
  await page.locator('.geo-publication-panel').scrollIntoViewIfNeeded();
  await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-geocaching-publication-pending.png',fullPage:true});
  await page.getByRole('button',{name:'停止原任务',exact:true}).click();
  await page.getByRole('group',{name:'确认停止任务',exact:true}).waitFor();assert.equal(publicationStopCalls,0);
  const stopCalls=calls.length;
  for(const locale of locales){const text=geocachingPublicationText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByRole('group',{name:text.confirmStop,exact:true}).waitFor();await page.getByText(text.stopHelp,{exact:true}).waitFor();assert.equal(publicationStopCalls,0);assert.equal(calls.length,stopCalls);}
  await page.locator('.management-language select').selectOption('zh-Hans');
  await page.getByRole('button',{name:'保留任务',exact:true}).click();assert.equal(publicationStopCalls,0);
  await page.getByRole('button',{name:'停止原任务',exact:true}).click();
  await page.getByRole('button',{name:'确认本地停止',exact:true}).click();
  await page.getByText('停止待确认',{exact:true}).waitFor();assert.equal(publicationStopCalls,1);
  await page.reload();await page.getByRole('button',{name:'草稿',exact:true}).click();
  await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('USB 作者已验证',{exact:true}).waitFor();
  await page.getByText('浏览器上次记录：停止待确认',{exact:true}).waitFor();
  assert.equal(await page.getByRole('button',{name:'恢复原提交',exact:true}).count(),0);
  await page.getByRole('button',{name:'查询原任务',exact:true}).click();await page.getByText('停止待确认',{exact:true}).waitFor();
  await page.getByRole('button',{name:'停止原任务',exact:true}).click();await page.getByRole('button',{name:'确认本地停止',exact:true}).click();
  await page.getByText('本地已停止',{exact:true}).waitFor();assert.equal(publicationStopCalls,3);assert.equal(publicationCommits,1);
  await page.locator('.geo-publication-panel').scrollIntoViewIfNeeded();
  await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-geocaching-publication-stopped.png',fullPage:true});
  publicationConfirmed=true;catalogEpoch++;
  await page.getByRole('button',{name:'查询原任务',exact:true}).click();await page.getByText('目录已确认',{exact:true}).waitFor();
  await page.getByRole('button',{name:'我的藏宝点',exact:true}).click();await page.locator('.geo-draft-row').waitFor();
  assert.equal(await page.locator('.geo-draft-row').count(),1);assert.equal(publicationCommits,1);
  await page.getByRole('button',{name:'复制藏宝点 ID',exact:true}).click();
  assert.equal(await page.evaluate(()=>window.testCopiedId),idText(publicationTask.cache));
  await page.locator('.geo-publication-panel').scrollIntoViewIfNeeded();
  await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-geocaching-published.png',fullPage:true});
  await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'publication layout overflow');
  await page.setViewportSize({width:1440,height:960});
  await page.getByRole('button',{name:'浏览',exact:true}).click();
  assert.equal(await page.locator('#geocaching-browse-controls').isVisible(),true);
  assert.equal(await page.locator('.geo-draft-detail').count(),0);
  await page.getByRole('button',{name:'草稿',exact:true}).click();await page.getByRole('button',{name:'刷新设备列表',exact:true}).click();await page.locator('.geo-draft-row').first().waitFor();assert.equal(await page.locator('.geo-draft-row').count(),1);
  await page.locator('.geo-draft-row').first().click();await page.getByLabel('名称',{exact:true}).fill('保留旧作者编辑');
  await page.getByRole('button',{name:'查询原任务',exact:true}).click();
  await page.getByText('目录已接受此版本，设备列表已刷新。',{exact:true}).waitFor();
  assert.equal(await page.getByLabel('名称',{exact:true}).inputValue(),'保留旧作者编辑','querying another publication discarded unsaved edits');
  await page.getByRole('button',{name:'断开 USB',exact:true}).click();
  assert.equal(await page.locator('.geo-draft-row').count(),0);
  activeIdentity=await Identity.generate();const nextKey=await activeIdentity.getPublicKey();
  const nextDestination=await Identity.truncatedHash(Uint8Array.from([...nameHash,...activeIdentity.identityHash]));
  activeInfo=[nextKey,activeIdentity.identityHash,nextDestination,9];
  await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('USB 作者已验证',{exact:true}).waitFor();
  assert.equal(await page.getByLabel('名称',{exact:true}).inputValue(),'保留旧作者编辑');
  assert.equal(await page.getByLabel('名称',{exact:true}).isDisabled(),true);
  assert.equal(await page.getByRole('button',{name:'保存草稿到设备',exact:true}).isDisabled(),true);
  assert.ok(calls.includes(64)&&calls.includes(65)&&calls.includes(66)&&calls.filter(op=>op===3).length>=3);
  assert.deepEqual(errors,[]);console.log('Geocaching maintenance browser checks passed.');
}finally{await browser.close();await new Promise(r=>server.close(r));}
