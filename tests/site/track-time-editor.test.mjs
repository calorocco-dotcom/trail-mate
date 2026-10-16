import test from 'node:test';
import assert from 'node:assert/strict';
import {kmlTimeEditDate,compareKmlTimeEditDates,validateKmlTimePatch,activeKmlTimeValues} from '../../site/tracks/src/kml-time-edit.js';
import {kmlTimeEditorText,kmlTimeEditorKeys} from '../../site/i18n/kml-time-editor-labels.js';
import {locales} from '../../site/i18n/registry.js';
test('time edit ordering preserves submillisecond fractions and supports XML calendar years outside JS Date range',()=>{
 assert.equal(compareKmlTimeEditDates('2026-01-01T08:00:00+08:00','2026-01-01T00:00:00Z'),0);
 assert.equal(compareKmlTimeEditDates('2026-01-01T00:00:00.123456Z','2026-01-01T00:00:00.123457Z'),-1);
 assert.equal(compareKmlTimeEditDates('2026-01-01T00:00:00.1Z','2026-01-01T00:00:00.1000Z'),0);
 assert.equal(compareKmlTimeEditDates('2024-02-29T24:00:00Z','2024-03-01T00:00:00Z'),0);
 assert.equal(compareKmlTimeEditDates('1000000','1000001'),-1);assert.equal(compareKmlTimeEditDates('-0002','-0001'),-1);
 assert.equal(compareKmlTimeEditDates('-0001-02-29','-0001-03-01'),-1);assert.equal(compareKmlTimeEditDates('0001','-0001'),1);
 assert.equal(kmlTimeEditDate('2026-02').seconds,kmlTimeEditDate('2026-02-01T00:00:00').seconds);
 for(const value of ['0000','2026-02-30','2026-01-01T24:00:00.01Z','2026-01-01T00:00:00+14:01','2026-01-01T00:00:60Z','2026\u00a0','x',''])assert.throws(()=>kmlTimeEditDate(value),{code:'KML_TIME_EDIT_INVALID'});
});
test('patch validation distinguishes inherit, stamps and one-sided spans without accepting unknown fields',()=>{
 for(const patch of [{mode:'inherit'},{mode:'stamp',when:'2026'},{mode:'span',begin:'',end:'2026'},{mode:'span',begin:'2026',end:''},{mode:'span',begin:'2026',end:'2027'},{mode:'span',begin:'2026-01-01T00:00:00.123456Z',end:'2026-01-01T00:00:00.123457Z'}])assert.equal(validateKmlTimePatch(patch),patch);
 for(const patch of [{mode:'span',begin:'',end:''},{mode:'span',begin:'2027',end:'2026'},{mode:'span',begin:'2026',end:'2026-01-01T00:00:00Z'},{mode:'stamp',when:''},{mode:'stamp',when:'bad'},{mode:'unknown'},{mode:'inherit',targetId:'change'}])assert.throws(()=>validateKmlTimePatch(patch),{code:'KML_TIME_EDIT_INVALID'});
 assert.deepEqual(activeKmlTimeValues({mode:'inherit',when:'inactive'}),{mode:'inherit'});assert.deepEqual(activeKmlTimeValues({mode:'stamp',when:'2026',begin:'inactive'}),{mode:'stamp',when:'2026'});
});
test('time editor copy and KML object names are complete in eleven languages',()=>{for(const {id} of locales){const text=kmlTimeEditorText(id);assert.deepEqual(Object.keys(text),kmlTimeEditorKeys);for(const value of Object.values(text))assert.ok(value.trim());assert.ok(text.inherited.includes('{owner}'));assert.ok(text.lossConfirm.includes('{items}'));}});
