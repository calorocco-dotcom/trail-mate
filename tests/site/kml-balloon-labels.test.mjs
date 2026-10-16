import test from 'node:test';
import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {kmlBalloonText} from '../../site/i18n/kml-balloon-labels.js';
test('balloon editing and preview have complete labels for all eleven languages',()=>{
 for(const {id}of locales){const text=kmlBalloonText(id);assert.equal(Object.keys(text).length,19);assert.ok(Object.values(text).every(value=>typeof value==='string'&&value.length));assert.notEqual(text.show,text.hide);assert.ok(text.unknown.includes('{tokens}'));assert.ok(text.invalidTemplate.includes('64 KiB'));}
 assert.throws(()=>kmlBalloonText('unknown'));
});
