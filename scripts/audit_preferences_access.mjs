import {readdir,readFile,writeFile} from 'node:fs/promises';
import {fileURLToPath,pathToFileURL} from 'node:url';
import path from 'node:path';

// Keep positions and newlines while excluding comments and quoted content
// from receiver/call discovery. Arguments are read from the original source.
export function maskStorageSource(source,{preserveStrings=false}={}){
 const out=source.split('');let state='code',quote='',escaped=false;
 for(let i=0;i<source.length;i++){
  const c=source[i],next=source[i+1];
  if(state==='line'){if(c==='\n')state='code';else out[i]=' ';continue;}
  if(state==='block'){if(c==='*'&&next==='/'){out[i]=out[i+1]=' ';i++;state='code';}else if(c!=='\n')out[i]=' ';continue;}
  if(state==='string'){if(!preserveStrings&&c!=='\n')out[i]=' ';if(escaped)escaped=false;else if(c==='\\')escaped=true;else if(c===quote)state='code';continue;}
  if(c==='/'&&next==='/'){out[i]=out[i+1]=' ';i++;state='line';}
  else if(c==='/'&&next==='*'){out[i]=out[i+1]=' ';i++;state='block';}
  else if(c==='"'||c==="'"){if(!preserveStrings)out[i]=' ';state='string';quote=c;}
 }
 return out.join('');
}
export function storageArguments(source,masked,start){
 const args=[];let begin=start,depth=0;
 for(let i=start;i<masked.length;i++){
  const c=masked[i];
  if(c==='('||c==='['||c==='{')depth++;
  else if(c===')'&&!depth){args.push(source.slice(begin,i).trim());return args;}
  else if(c===')'||c===']'||c==='}')depth--;
  else if(c===','&&!depth){args.push(source.slice(begin,i).trim());begin=i+1;}
 }
 return [];
}
export function scanPreferencesAccesses(source,file){
 const masked=maskStorageSource(source),receivers=new Set([...masked.matchAll(/\b(?:Preferences|ConfigPreferences)\s*(?:[&*]\s*)?(\w+)/g)].map(match=>match[1]));
 const rows=[];
 for(const match of masked.matchAll(/\b(\w+)\s*\.\s*(begin|get\w+|put\w+|isKey|remove|clear)\s*\(/g)){
  if(!receivers.has(match[1]))continue;
  const args=storageArguments(source,masked,match.index+match[0].length),method=match[2];
  const kind=method==='begin'?'namespace':method==='clear'?'clear':'key';
  // Never include defaults, values, payloads, read buffers or credentials.
  const expression=kind==='clear'?'':args[0];if(expression===undefined)continue;
  const literal=expression.match(/^"([a-zA-Z0-9_.-]+)"$/)?.[1]??null;
  rows.push({file,line:source.slice(0,match.index).split('\n').length,receiver:match[1],method,kind,literal,expression});
 }
 return rows;
}
export async function runPreferencesAudit(root,{check=false}={}){
 const pending=['platform/esp','modules/ui_shared'],files=[],rows=[];
 while(pending.length){const folder=pending.pop();for(const entry of await readdir(path.join(root,folder),{withFileTypes:true})){const file=folder+'/'+entry.name;if(entry.isDirectory())pending.push(file);else if(/\.(?:cpp|c|h|hpp)$/.test(entry.name))files.push(file);}}
 files.sort();for(const file of files)rows.push(...scanPreferencesAccesses(await readFile(path.join(root,file),'utf8'),file));
 const opens=rows.filter(row=>row.kind==='namespace'),keys=rows.filter(row=>row.kind==='key'),clears=rows.filter(row=>row.kind==='clear');
 const escape=value=>String(value).replaceAll('|','\\|').replaceAll('\n',' ').replaceAll('`','');
 const reference=row=>`[${row.file}:${row.line}](../../../../${row.file}#L${row.line})`;
 const document=['# 直接 Preferences 持久化入口清单','',
  '由 scripts/audit_preferences_access.mjs 扫描 ESP 与共享 UI 源码中声明为 Preferences / ConfigPreferences 的对象及引用参数，提取显式成员调用。排除普通注释和字符串中的伪调用，只记录 namespace/key 实参表达式、方法和位置，不输出默认值、配置值、凭据或字节内容。','',
  '这是候选入口清单，不是完整编译器调用图或配置覆盖证明。对象作用域、条件编译、宏、跨文件别名、箭头访问、裸 NVS API、第三方库及其他平台仍需核对；尤其不把同名对象上最近一次 begin 自动当作后续键的 namespace，以免作用域或分支造成错误归类。原始字符串、复杂模板和预处理器结构亦须人工复核。动态实参全部保留。','',
  `扫描 ${files.length} 个源文件，发现 ${rows.length} 个调用：namespace 开启 ${opens.length}，键访问 ${keys.length}，整域清理 ${clears.length}。键访问中 ${keys.filter(row=>row.literal).length} 个为直接字面量，${keys.filter(row=>!row.literal).length} 个仍需追踪常量或动态表达式。`,'',
  '## Namespace 开启','', '| 接收对象 | Namespace 实参 | 来源 |','|---|---|---|',
  ...opens.map(row=>`| ${row.receiver} | ${escape(row.expression)} | ${reference(row)} |`),'',
  '## 键访问','', '| 接收对象 | 方法 | 键实参 | 字面量键 | 来源 |','|---|---|---|---|---|',
  ...keys.map(row=>`| ${row.receiver} | ${row.method} | ${escape(row.expression)} | ${row.literal??'待追踪'} | ${reference(row)} |`),'',
  '## 整域清理','', '| 接收对象 | 来源 |','|---|---|',...clears.map(row=>`| ${row.receiver} | ${reference(row)} |`),''].join('\n');
 const output=path.join(root,'docs/locales/zh-Hans/web-outdoor-management/25-直接Preferences入口清单.md');
 if(check){if((await readFile(output,'utf8')).replaceAll('\r\n','\n')!==document)throw Error('Preferences access inventory stale; regenerate and review.');}
 else await writeFile(output,document);
 return {files:files.length,calls:rows.length,namespaces:opens.length,keys:keys.length,clears:clears.length};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
 const result=await runPreferencesAudit(fileURLToPath(new URL('../',import.meta.url)),{check:process.argv.includes('--check')});
 console.log(`Preferences access inventory: ${JSON.stringify(result)}; no device values read.`);
}
