import test from 'node:test';import assert from 'node:assert/strict';import {readFile} from 'node:fs/promises';
import {locales} from '../../site/i18n/registry.js';import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';import {configurationField} from '../../site/device/src/config-schema.js';
import {execFile} from 'node:child_process';import {promisify} from 'node:util';import {fileURLToPath} from 'node:url';
test('configuration inventory matches the current firmware source without regenerating it',async()=>{
 await promisify(execFile)(process.execPath,[fileURLToPath(new URL('../../scripts/audit_web_config_fields.mjs',import.meta.url)),'--check']);
});
// The separately generated source inventory is a checked-in documentation
// artifact, not proof of every independent store or every field constraint.
test('documented TMS families render across eleven languages without Chinese fallback',async()=>{
 const document=await readFile(new URL('../../docs/locales/zh-Hans/web-outdoor-management/15-配置字段覆盖核对.md',import.meta.url),'utf8');
 const keys=document.split(/\r?\n/).filter(line=>line.startsWith('| ')&&!line.startsWith('| 字段族')).map(line=>line.split('|')[1].trim());
 assert.ok(keys.length>=200);assert.equal(new Set(keys).size,keys.length);
 for(const {id} of locales)for(const family of keys){const key=family.replace('{slot}','0'),result=configurationField({key,type:'u8'},configurationPresentation(key,id));assert.ok(result.label.trim(),`${id}: ${family}`);if(!['zh-Hans','zh-Hant-TW','ja'].includes(id))assert.ok(!/[\u3400-\u9fff]/.test(result.label),`${id}: ${family} = ${result.label}`);}
});
