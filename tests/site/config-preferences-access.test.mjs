import test from 'node:test';import assert from 'node:assert/strict';
import {scanPreferencesAccesses} from '../../scripts/audit_preferences_access.mjs';
test('direct Preferences audit records keys and namespaces without values or defaults',()=>{
 const source=`Preferences prefs;
 prefs.begin("chat", true);
 prefs.getString("name", "DEFAULT_PRIVATE");
 prefs.putString(kPasswordKey, "SECRET_VALUE");
 prefs.putBytes(dynamic_key(slot, nested(1, 2)), buffer, length);
 prefs.clear();`;
 const rows=scanPreferencesAccesses(source,'fixture.cpp');assert.equal(rows.length,5);
 assert.deepEqual(rows.map(row=>[row.method,row.kind,row.literal]),[['begin','namespace','chat'],['getString','key','name'],['putString','key',null],['putBytes','key',null],['clear','clear',null]]);
 assert.equal(rows[3].expression,'dynamic_key(slot, nested(1, 2))');assert.equal(rows[3].line,5);
 assert.ok(!/DEFAULT_PRIVATE|SECRET_VALUE|buffer|length/.test(JSON.stringify(rows)));
});
test('comments, quoted decoys and unrelated objects cannot become storage calls',()=>{
 const source=`// Preferences fake; fake.getString("decoy", "secret");
 /* Preferences ghost; ghost.begin("decoy"); */
 const char* text="prefs.getString(\\\"decoy\\\", \\\"secret\\\")";
 platform::esp::common::ConfigPreferences prefs;
 other.getString("unrelated", "secret");
 prefs.getString("real", "secret");`;
 const rows=scanPreferencesAccesses(source,'fixture.cpp');assert.equal(rows.length,1);assert.equal(rows[0].literal,'real');assert.equal(rows[0].line,6);
});
test('reference parameters, multiline calls and argument comments retain only first argument',()=>{
 const source=`bool load(Preferences& store){
 store.begin(ns, true);
 store.getBytes(
   key,
   /* comma , and ) inside comment */ output, sizeof(output));
 store.remove("old");
 return true;
 }`;
 const rows=scanPreferencesAccesses(source,'fixture.cpp');assert.equal(rows.length,3);assert.equal(rows[1].expression,'key');assert.equal(rows[2].literal,'old');assert.equal(rows[0].literal,null);
});
