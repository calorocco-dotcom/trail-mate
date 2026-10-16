import test from 'node:test';import assert from 'node:assert/strict';
import {configurationMaskOptions,toggleConfigurationMask} from '../../site/device/src/config-mask.js';
import {configMaskHelp} from '../../site/i18n/config-mask-labels.js';import {locales} from '../../site/i18n/registry.js';
test('satellite selectors match firmware bits and preserve unknown bits',()=>{
 assert.deepEqual(configurationMaskOptions('gps.sat_mask').map(option=>option.value),[1,2,4,8]);assert.equal(configurationMaskOptions('other.mask'),null);
 assert.equal(toggleConfigurationMask(0x8d,2,true),0x8f);assert.equal(toggleConfigurationMask(0x8f,8,false),0x87);assert.equal(toggleConfigurationMask(0x80,1,true),0x81);
 assert.throws(()=>toggleConfigurationMask(256,1,true));for(const locale of locales)assert.ok(configMaskHelp(locale.id).length>10);
});
