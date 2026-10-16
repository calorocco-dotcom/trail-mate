import {TmsDocument} from './tms-document.js';
import {configurationField} from './config-schema.js';
import {equal} from '../../geocaching/src/protocol.js';
import {CONFIG_COLLECTIONS,restoreConfigCollection,configurationChanges} from './config-collections.js';
import {validateKnownModuleDocument} from './config-module-validation.js';
import {validateC6Configuration} from './config-c6-validation.js';
import {setConfigurationValue,synchronizeMeshCoreChannelMirror} from './config-editor-values.js';
import {isWebConfigurationField} from './config-web-scope.js';
import {preserveExcludedModuleConfiguration} from './config-module-web-scope.js';
const same=(a,b)=>a instanceof Uint8Array&&b instanceof Uint8Array?equal(a,b):a===b;

export function prepareConfigImport(current,backup){
 if(!(current instanceof TmsDocument)||!(backup instanceof TmsDocument)||backup.kind!=='backup')throw Error('请选择 Trail Mate 配置备份文件');
 let candidate=new TmsDocument(current.serialize({kind:'working'}));
 const changed=[],skipped=[],errors=[];
 const handled=new Set(),sourceEntries=backup.entries(),currentEntries=current.entries();
 for(const [name,spec] of Object.entries(CONFIG_COLLECTIONS)){
  const present=sourceEntries.some(r=>r.key===spec.count),offered=currentEntries.some(r=>r.key===spec.count);
  if(!present&&!offered)continue;
  handled.add(name);
  if(!present){if(sourceEntries.some(r=>r.key.startsWith(spec.prefix)))errors.push({key:spec.count,reason:'备份集合缺少数量，不能部分覆盖'});continue;}
  if(!offered){for(const record of sourceEntries.filter(r=>r.key===spec.count||r.key.startsWith(spec.prefix)))skipped.push({key:record.key,reason:'此设备未提供该集合能力'});continue;}
  try{
   const restored=restoreConfigCollection(candidate,backup,name);candidate=restored.document;skipped.push(...restored.skipped);
  }catch(error){errors.push({key:error.field??spec.count,reason:error.message});}
 }
 const target=new Map(candidate.entries().map(r=>[r.key,r]));
 for(const source of backup.entries()){
   if(['schema.version','document.kind'].includes(source.key))continue;
   if(!isWebConfigurationField(source.key)){skipped.push({key:source.key,reason:'此字段不在网页管理范围，保留设备值'});continue;}
   if([...handled].some(name=>source.key===CONFIG_COLLECTIONS[name].count||source.key.startsWith(CONFIG_COLLECTIONS[name].prefix)))continue;
   const existing=target.get(source.key);
   if(!existing){skipped.push({key:source.key,reason:'此设备未包含该配置字段'});continue;}
   if(source.type!==existing.type){errors.push({key:source.key,reason:'字段类型与设备不一致'});continue;}
   if(source.value===undefined||existing.value===undefined||configurationField(existing).readOnly){skipped.push({key:source.key,reason:'由设备管理或暂不支持编辑，保留设备值'});continue;}
   if(same(source.value,existing.value))continue;
   try{setConfigurationValue(candidate,source.key,source.value);}
   catch(error){errors.push({key:source.key,reason:error.message});}
 }
 synchronizeMeshCoreChannelMirror(candidate);
 try{const retained=preserveExcludedModuleConfiguration(current,candidate);candidate=retained.document;if(retained.changed)skipped.push({key:'mt_module',reason:'模块中的网页范围外兼容参数保留设备当前值'});validateKnownModuleDocument(candidate);}catch(error){errors.push({key:'mt_module',reason:error.code??error.message});}
 try{validateC6Configuration(candidate);}catch(error){errors.push({key:error.field??'c6_companion',reason:error.code??error.message});}
 if(errors.length)return {document:new TmsDocument(current.serialize({kind:'working'})),changed:[],skipped,errors};
 const finalEntries=new Map(candidate.entries().map(r=>[r.key,r])),originalEntries=new Map(currentEntries.map(r=>[r.key,r]));
 for(const key of configurationChanges(candidate,current)){
  const record=finalEntries.get(key)??originalEntries.get(key),schema=configurationField(record);
  changed.push({key,label:schema.label,secret:schema.secret,removed:!finalEntries.has(key),added:!originalEntries.has(key)});
 }
 return {document:candidate,changed,skipped,errors};
}
