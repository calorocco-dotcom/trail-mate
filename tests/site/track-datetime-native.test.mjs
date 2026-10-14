import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {validTrackDateTime} from '../../site/tracks/src/track-datetime.js';
const exe=path.resolve('.codex-build/hostlink-codec/native/track_datetime'+(process.platform==='win32'?'.exe':''));
test('web and C++ agree on calendar, timezone, unknown and precision policy',()=>{
 const cases=[['2024-02-29T12:00:00Z',true],['2026-02-31T00:00:00Z',false],['1900-02-29T00:00:00Z',false],['2000-02-29T00:00:00Z',true],['-0001-02-29T00:00:00Z',true],['10000-01-01T00:00:00',true],['01000-01-01T00:00:00Z',false],['0000-01-01T00:00:00Z',false],['2026-10-11T24:00:00.000',true],['2026-10-11T24:00:00.001',false],['2026-10-11T00:00:00+14:00',true],['2026-10-11T00:00:00+14:01',false],['2026-10-11T00:00:60Z',false],['2026-10-11',false],[' ',true],['\u00a0',false],['2026-10-11T00:00:00.'+'0'.repeat(50)+'Z',false]];
 for(const [text,valid]of cases){assert.equal(validTrackDateTime(text,{allowUnknown:true}),valid,text);const result=spawnSync(exe,[text]);if(result.error)throw result.error;assert.equal(result.status,valid?0:1,text);}
 assert.equal(validTrackDateTime(''),false);
});
