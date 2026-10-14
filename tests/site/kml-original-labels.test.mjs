import test from 'node:test';import assert from 'node:assert/strict';
import {locales}from'../../site/i18n/registry.js';import{kmlOriginalText}from'../../site/i18n/kml-original-labels.js';
test('original preservation and unavailable legacy originals have distinct complete copy in eleven languages',()=>{for(const{id}of locales){const text=kmlOriginalText(id);assert.equal(Object.keys(text).length,2);assert.ok(Object.values(text).every(value=>typeof value==='string'&&value.length));assert.notEqual(text.help,text.legacy);}assert.throws(()=>kmlOriginalText('missing'));});
