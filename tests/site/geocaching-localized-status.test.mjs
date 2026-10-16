import test from 'node:test';
import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {geocachingStatusKeys,geocachingStatusLabels,geocachingStatusText} from '../../site/i18n/geocaching-status-labels.js';
import {createLocalizedStatus} from '../../site/geocaching/src/localized-status.js';
test('all dynamic catalogs are complete and retain interpolation parameters',()=>{
 assert.deepEqual(Object.keys(geocachingStatusLabels).sort(),locales.map(locale=>locale.id).sort());
 const english=geocachingStatusText('en'),parameters=value=>[...value.matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort();
 assert.equal(new Set(geocachingStatusKeys).size,geocachingStatusKeys.length);
 for(const locale of locales){const text=geocachingStatusText(locale.id);assert.deepEqual(Object.keys(text),geocachingStatusKeys);for(const key of geocachingStatusKeys){assert.ok(text[key].trim());assert.deepEqual(parameters(text[key]),parameters(english[key]));}}
 assert.throws(()=>geocachingStatusText('missing'),/Incomplete/);
});
test('descriptors retain numeric facts and diagnostic suffix across locale changes',()=>{
 let locale='en';const nodes={notice:{textContent:''},directory:{textContent:''}};
 const presentation=createLocalizedStatus({getText:()=>geocachingStatusText(locale),getNode:id=>nodes[id],formatValue:value=>typeof value==='number'?new Intl.NumberFormat(locale).format(value):String(value)});
 const values={succeeded:1234,failed:2};presentation.set('notice','downloadPartial',values,'deadbeef: diagnostic');values.succeeded=0;
 presentation.set('directory','verifiedDirectories',{count:1234});
 const retained=presentation.get('notice');retained.values.failed=99;
 for(const item of locales){locale=item.id;presentation.refresh();assert.equal(presentation.get('notice').values.failed,2);assert.equal(presentation.get('notice').values.succeeded,1234);assert.ok(nodes.notice.textContent.endsWith('\ndeadbeef: diagnostic'));assert.ok(nodes.directory.textContent.includes(new Intl.NumberFormat(locale).format(1234)));}
 presentation.raw('notice','original transport diagnostic');locale='en';presentation.refresh();assert.equal(nodes.notice.textContent,'original transport diagnostic');assert.equal(presentation.get('notice'),null);
 assert.throws(()=>presentation.set('notice','missing'),/Missing Geocaching status translation/);
 assert.throws(()=>presentation.set('absent','ready'),/Missing Geocaching status target/);
 assert.equal(presentation.get('absent'),null);
});
