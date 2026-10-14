import test from 'node:test';import assert from 'node:assert/strict';
import {trackTransferText} from '../../site/i18n/track-transfer-labels.js';import {locales} from '../../site/i18n/registry.js';
test('network file phases and retained pause have complete eleven-language copy',()=>{for(const {id} of locales){const text=trackTransferText(id);assert.equal(Object.keys(text).length,6);for(const value of Object.values(text))assert.ok(value.trim());}assert.throws(()=>trackTransferText('missing'));});
