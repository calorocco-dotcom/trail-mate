import {readFile,writeFile} from 'node:fs/promises';
import {fileURLToPath,pathToFileURL} from 'node:url';
import path from 'node:path';
import {maskStorageSource,storageArguments} from './audit_preferences_access.mjs';

const storeFile='platform/esp/arduino_common/src/app_config_store.cpp',codecFile='modules/core_sys/src/app/tms_config_codec.cpp';
const aliases={chat_policy:'chat_policy',meshtastic_config:'meshtastic_config',meshcore_config:'meshcore_config',rnode_config:'rnode_config',motion_config:'motion_config',aprs:'aprs'};
function body(source,name){
 const masked=maskStorageSource(source),start=masked.lastIndexOf('bool '+name+'(');
 if(start<0)throw Error('Missing production function '+name);
 const open=masked.indexOf('{',start);let depth=1,end=open+1;
 while(end<masked.length&&depth){if(masked[end]==='{')depth++;else if(masked[end]==='}')depth--;end++;}
 if(depth)throw Error('Unclosed production function '+name);return {source:source.slice(open+1,end-1),offset:open+1};
}
function member(expression,mesh){
 const value=expression.match(/\b(config|mesh|aprs|chat_policy|meshtastic_config|meshcore_config|rnode_config|motion_config)\.([a-zA-Z_][\w.]*)/);
 if(value)return value[1]==='config'?value[2]:value[1]==='mesh'?mesh+'.'+value[2]:(aliases[value[1]]??value[1])+'.'+value[2];
 const bare=expression.match(/^\s*([a-zA-Z_]\w*)\s*$/)?.[1];return bare??null;
}
export function collectCoreProjection(codec){
 const fields=new Map(),add=(key,owner)=>{if(!owner)return;const set=fields.get(owner)??new Set();set.add(key);fields.set(owner,set);};
 for(const [name,mesh] of [['writeGeneral',null],['writeMeshCore','meshcore_config'],['writeReticulum','rnode_config']]){
  const {source}=body(codec,name),masked=maskStorageSource(source);
  for(const call of masked.matchAll(/\bencoder\.(?:boolean|u8|u16|u32|i8|f32|text|blob)\s*\(/g)){
   const args=storageArguments(source,masked,call.index+call[0].length),key=args[0]?.match(/^"([\w.]+)"$/)?.[1];if(key)add(key,member(args[1]??'',mesh));
  }
 }
 const base=body(codec,'writeMeshBase'),masked=maskStorageSource(base.source);
 for(const call of masked.matchAll(/\bTMS_MESH_VALUE\s*\(/g)){
  const args=storageArguments(base.source,masked,call.index+call[0].length),suffix=args[1]?.match(/^"([\w.]+)"$/)?.[1],owner=member(args[2]??'');
  if(suffix&&owner)for(const [prefix,profile] of [['mt.','meshtastic_config'],['mc.base.','meshcore_config'],['rt.','rnode_config']])add(prefix+suffix,profile+'.'+owner);
 }
 for(const [prefix,profile] of [['mt.','meshtastic_config'],['mc.base.','meshcore_config'],['rt.','rnode_config']])for(const side of ['primary','secondary']){
  // These two explicit blob writes use snprintf rather than the value macro.
  if(!base.source.includes('"%s'+side+'_psk"'))throw Error('PSK projection changed');add(prefix+side+'_psk',profile+'.'+side+'_key');
 }
 const allMasked=maskStorageSource(codec),suffixes=['enabled','uplink','downlink','host','port','root','username','password'];
 for(const call of allMasked.matchAll(/\bwriteMqtt\s*\(/g)){
  const args=storageArguments(codec,allMasked,call.index+call[0].length),prefix=args[1]?.match(/^"(mt\.mqtt\.|mc\.mqtt\.)"$/)?.[1];if(!prefix)continue;
  if(args.length!==10)throw Error('MQTT projection changed');for(let i=0;i<8;i++)add(prefix+suffixes[i],member(args[i+2],prefix==='mc.mqtt.'?'meshcore_config':null));
 }
 return fields;
}
export function collectLegacyWrites(store,codec){
 const projection=collectCoreProjection(codec),constants=new Map(),withStrings=maskStorageSource(store,{preserveStrings:true});
 for(const match of withStrings.matchAll(/constexpr\s+const\s+char\s*\*\s*(k\w+)\s*=\s*"([\w.-]+)"\s*;/g)){
  if(constants.has(match[1]))throw Error('Ambiguous storage key constant');constants.set(match[1],match[2]);
 }
 const fn=body(store,'saveAppConfigToPreferences'),masked=maskStorageSource(fn.source),rows=[];
 for(const section of masked.matchAll(/\bif\s*\(save_(chat|gps|settings|aprs)\)\s*\{/g)){
  const namespace=section[1],begin=section.index+section[0].length;let depth=1,end=begin;
  while(end<masked.length&&depth){if(masked[end]==='{')depth++;else if(masked[end]==='}')depth--;end++;}
  if(depth)throw Error('Unclosed save domain');const source=fn.source.slice(begin,end-1),code=maskStorageSource(source);
  for(const call of code.matchAll(/\bput_(bool|uchar|char|ushort|uint|float|string|bytes)\s*\(/g)){
   const args=storageArguments(source,code,call.index+call[0].length);if(args.length<2)throw Error('Invalid save call');
   let key=args[0].match(/^"([\w.-]+)"$/)?.[1]??constants.get(args[0])??null,owner=member(args[1]),keys=projection.get(owner),classification='成员投影';
   if(args[0]==='key'){
    const previous=source.slice(0,call.index),formats=[...maskStorageSource(previous,{preserveStrings:true}).matchAll(/format_meshcore_channel_key\(key,\s*sizeof\(key\),\s*slot,\s*"(en|name|key)"\)/g)],suffix=formats.at(-1)?.[1];
    if(suffix){key='mc_ch{slot}_'+suffix;keys=new Set(['mc.channel.{slot}.'+(suffix==='en'?'enabled':suffix)]);classification='动态槽位，需核对读取及消费';}
   }
   if(namespace==='chat'&&key==='mc_ch_slot'){keys=new Set(['mc.active_slot']);classification='规范化槽位';}
   if(namespace==='chat'&&['mc_ch_name','mc_ch_key'].includes(key)){keys=new Set(['mc.channel.{active_slot}.'+(key.endsWith('name')?'name':'key')]);classification='旧活动槽镜像';}
   if(namespace==='chat'&&['pri_key_len','sec_key_len'].includes(key)){keys=new Set(['mt.'+(key.startsWith('pri')?'primary':'secondary')+'_psk']);classification='密钥长度元数据';}
   if(namespace==='chat'&&key==='mesh_protocol'){keys=new Set(['protocol.active']);classification='协议规范化';}
   if(namespace==='settings'&&key==='ble_enabled'&&args[1]==='false'){keys=new Set(['device.ble_enabled']);classification='旧BLE固定关闭值，独立BLE扩展负责真实配置';}
   // Never retain expression values, defaults, lengths, password text or buffers.
   const line=store.slice(0,fn.offset+begin+call.index).split('\n').length;
   rows.push({namespace,key:key??args[0],method:call[1],fields:keys?[...keys]:[],classification:keys?classification:'未解析',line});
  }
 }
 if(!rows.length)throw Error('No legacy save calls discovered');return rows;
}
export async function runAppConfigAudit(root,{check=false}={}){
 const rows=collectLegacyWrites(await readFile(path.join(root,storeFile),'utf8'),await readFile(path.join(root,codecFile),'utf8'));
 const missing=rows.filter(row=>!row.fields.length),escape=value=>String(value).replaceAll('|','\\|').replaceAll('\n',' ');
 const report=['# AppConfig 传统保存与核心 TMS 投影对账','',
  '由 scripts/audit_app_config_preferences.mjs 读取实际 saveAppConfigToPreferences 与核心 TMS writer。展开四个保存域中的包装器实参，解析同文件常量、成员投影与明确列出的动态槽／镜像／长度元数据；不记录配置值、默认值、秘密或缓冲内容，不读取设备。','',
  `找到 ${rows.length} 个保存调用；${missing.length} 个没有投影关系，必须继续追踪。调用数不是配置数；一个字段可对应多个元数据键。`,'',
  '范围：此表解决旧包装器实参不可见的缺口，只有保存→导出源码关系。不是编译器分析，不证明加载／TMS消费／校验／硬件生效或所有独立配置。前缀／MQTT固定参数位置、活动槽与长度元数据使用当前生产结构，改变时必须复核。Reticulum接口／组的专用持久化、独立BLE／模块／设置和账号密钥仍按23／24／26／44／46核对。删除的旧键不是新增可维护配置。','',
  '| Namespace | 存储键或模板 | 方法 | 核心TMS投影 | 关系 | 保存来源 |','|---|---|---|---|---|---|',
  ...rows.map(row=>`| ${row.namespace} | ${escape(row.key)} | ${row.method} | ${row.fields.join(', ')||'待追踪'} | ${row.classification} | [${storeFile}:${row.line}](../../../../${storeFile}#L${row.line}) |`),''].join('\n');
 const output=path.join(root,'docs/locales/zh-Hans/web-outdoor-management/47-AppConfig传统保存投影对账.md');
 if(check){if((await readFile(output,'utf8')).replaceAll('\r\n','\n')!==report)throw Error('AppConfig inventory stale');}else await writeFile(output,report);
 return {calls:rows.length,unresolved:missing.map(row=>({namespace:row.namespace,key:row.key,line:row.line}))};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href)console.log(JSON.stringify(await runAppConfigAudit(fileURLToPath(new URL('../',import.meta.url)),{check:process.argv.includes('--check')})));
