import test from 'node:test';
import assert from 'node:assert/strict';
import {findConfigurationFields} from '../../site/device/src/config-finder.js';
import {configFinderText} from '../../site/i18n/config-finder-labels.js';
import {locales} from '../../site/i18n/registry.js';
test('settings search spans groups, matches all tokens and never indexes secret values',()=>{
 const rows=[{key:'wifi.ssid',label:'无线网络名称',help:'Wi-Fi 接入',groupLabel:'网络与外设',value:'private-value'},
 {key:'gps.interval',label:'定位间隔',help:'GPS seconds',groupLabel:'GPS 与定位'}];
 assert.deepEqual(findConfigurationFields(rows,'网络 wi-fi'),[rows[0]]);
 assert.deepEqual(findConfigurationFields(rows,'GPS'),[rows[1]]);
 assert.deepEqual(findConfigurationFields(rows,'private-value'),[]);
 assert.deepEqual(findConfigurationFields(rows,'ＷＩＦＩ．ＳＳＩＤ'),[rows[0]]);
 assert.deepEqual(findConfigurationFields(rows,'',{changedOnly:true,changes:new Set(['gps.interval'])}),[rows[1]]);
 for(const locale of locales)assert.equal(Object.keys(configFinderText(locale.id)).length,7);
});
