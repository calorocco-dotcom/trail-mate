import {chromium} from '../../../../site/node_modules/playwright/index.mjs';
import {fileURLToPath,pathToFileURL} from 'node:url';
import path from 'node:path';
import fs from 'node:fs/promises';
const dir=path.dirname(fileURLToPath(import.meta.url));
await fs.mkdir(path.join(dir,'assets'),{recursive:true});
const browser=await chromium.launch({channel:'msedge',headless:true});
const page=await browser.newPage({viewport:{width:1440,height:1050},deviceScaleFactor:1});
const errors=[];page.on('pageerror',e=>errors.push(String(e)));
for(const view of ['welcome','config','cache','track','jobs','disconnect','conflict']){
  await page.goto(pathToFileURL(path.join(dir,'ui-preview.html')).href+'?view='+view);
  await page.screenshot({path:path.join(dir,'assets',view+'.png'),fullPage:true});
}
await page.goto(pathToFileURL(path.join(dir,'ui-preview.html')).href+'?view=config');
await page.getByRole('button',{name:'查看变更并保存'}).click();
await page.getByRole('button',{name:'保存到设备',exact:true}).click();
if(!await page.getByText('已保存并生效 · 配置版本 43').count())throw Error('Save interaction failed');
await page.setViewportSize({width:390,height:844});
await page.goto(pathToFileURL(path.join(dir,'ui-preview.html')).href+'?view=track');
await page.screenshot({path:path.join(dir,'assets','track-mobile.png'),fullPage:true});
if(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth))throw Error('Mobile overflow');
await browser.close();
if(errors.length)throw Error(errors.join('\n'));
console.log('Rendered 8 screenshots; save interaction and mobile overflow checks passed.');
