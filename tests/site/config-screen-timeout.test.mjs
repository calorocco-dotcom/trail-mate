import test from 'node:test';import assert from 'node:assert/strict';import {readFile} from 'node:fs/promises';
import {configurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configurationReview} from '../../site/device/src/config-review.js';
import {configPositionText} from '../../site/i18n/config-position-labels.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';import {locales} from '../../site/i18n/registry.js';
test('screen always-on sentinel is displayed as behavior rather than five minutes',async()=>{
 const firmware=await readFile(new URL('../../modules/core_sys/include/platform/ui/screen_power_state_machine.h',import.meta.url),'utf8');assert.match(firmware,/kMaxTimeoutMs\s*=\s*300000/);assert.match(firmware,/kAlwaysOnTimeoutMs\s*=\s*kMaxTimeoutMs/);
 const make=value=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nui.screen.timeout_ms=u32:'+value+'\nEND\n'));
 for(const locale of locales){const text=configPositionText(locale.id),options=configurationField({key:'ui.screen.timeout_ms',type:'u32'},configurationPresentation('ui.screen.timeout_ms',locale.id)).options;
  assert.deepEqual(options.map(row=>row.value),[15000,30000,60000,300000]);assert.equal(options.at(-1).label,text.always);assert.ok(!options.at(-1).label.includes('300'));assert.equal(configurationReview(make(300000),make(60000),locale.id)[0].after,text.always);
 }
});
