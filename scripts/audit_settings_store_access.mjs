import {readdir,readFile,writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const root=fileURLToPath(new URL('../',import.meta.url)),roots=['platform/esp','modules/ui_shared'];
async function sourceFiles(folder){
 const entries=await readdir(path.join(root,folder),{withFileTypes:true}),result=[];
 for(const entry of entries){const file=folder+'/'+entry.name;if(entry.isDirectory())result.push(...await sourceFiles(file));else if(/\.(?:cpp|c|h|hpp)$/.test(entry.name))result.push(file);}
 return result;
}
function argumentsOf(source,start){
 const args=[];let begin=start,depth=0,quote=null,escaped=false;
 for(let position=start;position<source.length;position++){
  const character=source[position];
  if(quote){if(escaped)escaped=false;else if(character==='\\')escaped=true;else if(character===quote)quote=null;continue;}
  if(character==='"'||character==="'"){quote=character;continue;}
  if(character==='('||character==='['||character==='{')depth++;
  if(character===')'&&!depth){args.push(source.slice(begin,position).trim());return args;}
  if(character===')'||character===']'||character==='}')depth--;
  if(character===','&&!depth){args.push(source.slice(begin,position).trim());begin=position+1;}
 }
 return [];
}
function resolveSymbol(expression,constants){
 const literal=expression?.match(/^"([a-zA-Z0-9_.-]+)"$/);if(literal)return literal[1];
 if(/^k[A-Z]\w*$/.test(expression??'')&&constants.get(expression)?.size===1)return [...constants.get(expression)][0];
 return null;
}
export function scanSettingsAccesses(source,file){
 const constants=new Map();
 for(const match of source.matchAll(/\b(k[A-Z]\w*)\s*(?:\[\s*\])?\s*=\s*"([a-zA-Z0-9_.-]+)"/g)){
  if(!constants.has(match[1]))constants.set(match[1],new Set());constants.get(match[1]).add(match[2]);
 }
 const aliases=['settings_store',...[...source.matchAll(/namespace\s+(\w+)\s*=\s*(?:::)?platform::ui::settings_store\s*;/g)].map(match=>match[1])];
 const calls=new RegExp('\\b(?:'+aliases.join('|')+')\\s*::\\s*(get_int|get_bool|get_uint|get_string|get_string_into|get_blob|get_blob_into|read_string_into|read_blob_into|read_int_into|read_uint_into|read_u8_into|read_bool_into|put_int|put_bool|put_uint|put_string|put_blob)\\s*\\(','g'),rows=[];
 for(const match of source.matchAll(calls)){
  const args=argumentsOf(source,match.index+match[0].length);if(args.length<2)continue;
  rows.push({file,line:source.slice(0,match.index).split('\n').length,method:match[1],namespace:resolveSymbol(args[0],constants),key:resolveSymbol(args[1],constants),namespaceExpression:args[0],keyExpression:args[1]});
 }
 return rows;
}
const files=(await Promise.all(roots.map(sourceFiles))).flat().sort(),rows=[];
for(const file of files)rows.push(...scanSettingsAccesses(await readFile(path.join(root,file),'utf8'),file));
const known=rows.filter(row=>row.namespace&&row.key),dynamic=rows.filter(row=>!row.namespace||!row.key),groups=new Map();
for(const row of known){const key=row.namespace+'.'+row.key;if(!groups.has(key))groups.set(key,[]);groups.get(key).push(row);}
const escape=value=>String(value).replaceAll('|','\\|').replaceAll('\n',' ').replaceAll('`','');
const reference=row=>`[${row.file}:${row.line}](../../../../${row.file}#L${row.line})`;
const document=['# ESP 与共享 UI 持久化入口清单','',
 '由 scripts/audit_settings_store_access.mjs 从 platform/esp 和 modules/ui_shared 的 C/C++ 源文件提取 settings_store 及其同文件 namespace 别名的显式读写调用。仅解析字面量及同文件 k 开头的唯一字符串常量；不会展开宏、跨文件常量或动态键，不按文件名推断覆盖状态。所有动态入口保留在下表，必须人工追到实参。','',
 '这是一份源码候选入口清单，不是配置覆盖或实机验收证明。未包含直接 Preferences/NVS API、无 namespace 限定调用、其他平台、第三方库的存储；注释/条件编译及作用域仍需结合源码人工核对。默认值及调用的第三个以后参数不写入报告，且不读取设备、配置文件或私钥。不能仅凭某键有 TMS 同义项就认定导出、校验、写入及生效均已完成。','',
 `扫描 ${files.length} 个源文件，发现 ${rows.length} 个显式调用；解析 ${known.length} 个调用、${groups.size} 个 namespace/key 组合；${dynamic.length} 个动态入口保留待核对。`,'',
 '## 静态键与实际调用来源','', '| Namespace | 存储键 | 读写方法 | 来源 |','|---|---|---|---|',
 ...[...groups].sort(([a],[b])=>a.localeCompare(b)).map(([,items])=>`| ${items[0].namespace} | ${items[0].key} | ${[...new Set(items.map(row=>row.method))].sort().join(', ')} | ${items.map(reference).join(', ')} |`),'',
 '## 动态入口：不得忽略','', '| Namespace 表达式 | 键表达式 | 方法 | 来源 |','|---|---|---|---|',
 ...dynamic.map(row=>`| ${escape(row.namespaceExpression)} | ${escape(row.keyExpression)} | ${row.method} | ${reference(row)} |`),''].join('\n');
const output=path.join(root,'docs/locales/zh-Hans/web-outdoor-management/22-持久化入口清单.md');
if(process.argv.includes('--check')){if((await readFile(output,'utf8')).replaceAll('\r\n','\n')!==document)throw Error('Settings access inventory stale; regenerate and review.');}
else await writeFile(output,document);
console.log(`Settings access inventory: ${rows.length} calls, ${groups.size} static keys, ${dynamic.length} dynamic entries; no device values read.`);
