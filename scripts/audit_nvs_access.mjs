import {readdir,readFile,writeFile} from 'node:fs/promises';
import {fileURLToPath,pathToFileURL} from 'node:url';
import path from 'node:path';
import {maskStorageSource,storageArguments} from './audit_preferences_access.mjs';

export function scanNvsAccesses(source,file){
 const masked=maskStorageSource(source),rows=[];
 for(const match of masked.matchAll(/\b(nvs_(?:open(?:_from_partition)?|(?:get|set)_(?:u8|i8|u16|i16|u32|i32|u64|i64|str|blob)|erase_key|erase_all|commit|close))\s*\(/g)){
  const method=match[1],args=storageArguments(source,masked,match.index+match[0].length),opened=method==='nvs_open'||method==='nvs_open_from_partition',partitioned=method==='nvs_open_from_partition';
  const kind=opened?'namespace':/^nvs_(?:get_|set_|erase_key)/.test(method)?'key':method==='nvs_erase_all'?'clear':method==='nvs_commit'?'commit':'close';
  const expression=opened?args[partitioned?1:0]:kind==='key'?args[1]:'';
  const handle=opened?args[partitioned?3:2]:args[0];
  if(expression===undefined||handle===undefined)continue;
  // Only namespace/key identifiers and handle expressions. Never defaults,
  // payloads, output buffers, values or partition names.
  rows.push({file,line:source.slice(0,match.index).split('\n').length,method,kind,handle,expression,literal:expression.match(/^"([A-Za-z0-9_.-]+)"$/)?.[1]??null});
 }
 return rows;
}
export function scanNvsKeyAliases(source){
 const masked=maskStorageSource(source),table=masked.match(/constexpr\s+StorageKeyAlias\s+kStorageKeyAliases\[\]\s*=\s*\{([\s\S]*?)\};/);if(!table)throw Error('Missing authoritative NVS key alias table');
 const start=table.index+table[0].indexOf('{')+1,body=maskStorageSource(source,{preserveStrings:true}).slice(start,start+table[1].length);
 const pattern=/\{\s*"([A-Za-z0-9_.-]+)"\s*,\s*"([A-Za-z0-9_.-]+)"\s*\}/g;
 const entries=[...body.matchAll(pattern)].map(row=>({logical:row[1],stored:row[2]}));
 if(body.replace(pattern,'').replace(/[\s,]/g,''))throw Error('Unsupported NVS key alias entry');
 const limit=Number(masked.match(/kMaxNvsKeyLength\s*=\s*(\d+)/)?.[1]);
 if(!limit||!entries.length||new Set(entries.map(row=>row.logical)).size!==entries.length||entries.some(row=>row.stored.length>limit)||new Set(entries.map(row=>row.stored)).size!==entries.length)throw Error('Invalid NVS key alias table');
 return {limit,entries};
}
export async function collectNvsAccesses(root,roots=['platform/esp','modules/ui_shared','third_party']){
 const pending=[...roots],files=[],rows=[];
 while(pending.length){const folder=pending.pop();for(const entry of await readdir(path.join(root,folder),{withFileTypes:true})){const file=folder+'/'+entry.name;if(entry.isDirectory())pending.push(file);else if(/\.(?:c|cpp|h|hpp)$/.test(entry.name))files.push(file);}}
 files.sort();for(const file of files)rows.push(...scanNvsAccesses(await readFile(path.join(root,file),'utf8'),file));return {files,rows};
}
export async function runNvsAudit(root,{check=false}={}){
 const {files,rows}=await collectNvsAccesses(root),aliasFile='platform/esp/common/include/platform/esp/common/settings_store_impl.h',aliases=scanNvsKeyAliases(await readFile(path.join(root,aliasFile),'utf8'));
 const escape=value=>String(value).replaceAll('|','\\|').replaceAll('\n',' ').replaceAll('`','');
 const reference=row=>`[${row.file}:${row.line}](../../../../${row.file}#L${row.line})`;
 const document=['# 裸 NVS 入口与存储别名清单','',
  '由 scripts/audit_nvs_access.mjs 扫描 ESP、共享 UI 和仓库 third_party 的 C/C++ 文件。只记录 NVS 函数、namespace/key/handle 实参和位置，不读取设备、不记录值/默认值/缓冲/分区名。不把最近的 nvs_open 自动推断为后续调用的 namespace；跨作用域句柄、宏和条件编译仍需人工核对。','',
  `扫描 ${files.length} 个源文件，找到 ${rows.length} 个候选调用，分布于 ${new Set(rows.map(row=>row.file)).size} 个文件。显式打开 ${rows.filter(row=>row.kind==='namespace').length}，键访问 ${rows.filter(row=>row.kind==='key').length}，其余为提交/关闭/清域。调用数不是独立配置数。`,'',
  '这是源码入口清单，不是全部配置或硬件生效证明。系统 SDK、构建环境依赖、其他平台、函数指针/包装器实参以及复杂原始字符串不由此表自动覆盖。依赖库可单独调用 collectNvsAccesses 指定根目录审计；输出应单独记录其版本，不能写进与机器无关的仓库清单。','',
  '## NVS 候选调用','', '| 方法 | 类别 | 句柄表达式 | namespace/key 表达式 | 字面量 | 来源 |','|---|---|---|---|---|---|',
  ...rows.map(row=>`| ${row.method} | ${row.kind} | ${escape(row.handle)} | ${escape(row.expression)} | ${row.literal??'动态/不适用'} | ${reference(row)} |`),'',
  '## 通用存储键映射','',
  `实际 resolve_storage_key 在通用 settings_store 的读写入口使用这些别名；最大 NVS 键名为 ${aliases.limit} 字节。映射不是额外可编辑配置，也不能假设直接 Preferences 或其它库自动调用它。原逻辑键的 TMS/网页覆盖应与实际调用方继续对账。`,'',
  `来源：[${aliasFile}](../../../../${aliasFile})`,'', '| 逻辑键 | 实际 NVS 键 |','|---|---|',...aliases.entries.map(row=>`| ${row.logical} | ${row.stored} |`),''].join('\n');
 const output=path.join(root,'docs/locales/zh-Hans/web-outdoor-management/43-裸NVS入口与存储别名清单.md');
 if(check){if((await readFile(output,'utf8')).replaceAll('\r\n','\n')!==document)throw Error('NVS inventory stale; regenerate and review.');}else await writeFile(output,document);
 return {files:files.length,calls:rows.length,sources:[...new Set(rows.map(row=>row.file))],aliases:aliases.entries.length};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){const result=await runNvsAudit(fileURLToPath(new URL('../',import.meta.url)),{check:process.argv.includes('--check')});console.log('NVS source inventory: '+JSON.stringify(result)+'; no device values read.');}
