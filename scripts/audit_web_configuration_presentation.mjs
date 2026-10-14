import fs from 'node:fs/promises';import path from 'node:path';import {spawnSync} from 'node:child_process';
import {TmsDocument} from '../site/device/src/tms-document.js';
import {configurationField} from '../site/device/src/config-schema.js';
import {configurationPresentation} from '../site/device/src/config-field-presentation.js';
import {isWebConfigurationField} from '../site/device/src/config-web-scope.js';
import {isWebModuleField} from '../site/device/src/config-module-web-scope.js';
import {configurationMaskOptions} from '../site/device/src/config-mask.js';
import {MODULE_SCHEMA} from '../site/device/src/meshtastic-module-schema.js';
import {configModuleFieldLabel} from '../site/i18n/config-module-labels.js';
import {moduleDuration} from '../site/device/src/config-module-duration.js';
import {isModulePositionPrecision} from '../site/device/src/config-position-precision.js';
const executable=path.resolve('.codex-build/hostlink-codec/native/tms_complete_interop_cli'+(process.platform==='win32'?'.exe':''));
// This fixed CLI emits a synthetic fixture. Never accept device dumps as input;
// report schema/presentation metadata only, without configuration values.
const emitted=spawnSync(executable,['--emit'],{windowsHide:true,maxBuffer:1024*1024});if(emitted.error)throw emitted.error;if(emitted.status!==0)throw Error('Synthetic fixture CLI failed');
const document=new TmsDocument(new Uint8Array(emitted.stdout)),totals={},rows=[];
for(const record of document.entries()){
 if(!isWebConfigurationField(record.key))continue;const schema=configurationField(record,configurationPresentation(record.key,'zh-Hans'),document);
 const control=record.key.startsWith('mt_module.')?'module container':schema.readOnly?'read-only':schema.duration?'duration with unit':configurationMaskOptions(record.key)?'bit choices':schema.character?'character':schema.percentScale?'percent slider':schema.options?(schema.allowCustom?'choice with custom':'choice'):record.type==='bool'?'toggle':record.type==='b64'?'binary/text adapter':/^(u8|u16|u32|i8|i32|f32)$/.test(record.type)?'number':'text';
 totals[control]=(totals[control]??0)+1;rows.push({key:record.key,type:record.type,label:schema.label,control,range:schema.range??null,help:schema.help??null,unit:schema.duration?.nativeUnit??null});
}
const modules=[],excludedModules=[];function walk(message,names=[]){for(const field of MODULE_SCHEMA.messages[message].fields){const namesNext=[...names,field.name],key=namesNext.join('.');if(!isWebModuleField(namesNext)){excludedModules.push(key);continue;}const duration=moduleDuration(namesNext,field);modules.push({key,type:field.type,label:configModuleFieldLabel(namesNext,'zh-Hans'),readOnly:Boolean(field.readOnly),durationUnit:duration?.nativeUnit??null,positionPrecision:isModulePositionPrecision(namesNext,field)});if(field.type==='MESSAGE')walk(field.message,namesNext);}}walk(MODULE_SCHEMA.root);
const report={source:'synthetic complete production codec fixture; no hardware data and no configuration values',fields:document.size,webRecords:rows.length,excludedRecords:document.size-rows.length,totals,moduleDescriptors:modules.length,excludedModules,rows,modules};
await fs.writeFile('.codex-build/hostlink-codec/config-presentation-audit.json',JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({fields:report.fields,webRecords:report.webRecords,totals,moduleDescriptors:modules.length,excludedModules}));
