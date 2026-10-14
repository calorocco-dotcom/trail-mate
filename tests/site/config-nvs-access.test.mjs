import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {scanNvsAccesses,scanNvsKeyAliases,runNvsAudit} from '../../scripts/audit_nvs_access.mjs';

test('NVS audit retains namespace/key/handle expressions and excludes values and output buffers',()=>{
 const source=`nvs_open("settings", NVS_READWRITE, &handle);
 nvs_open_from_partition("PRIVATE_PARTITION", "lxmf_ident", NVS_READONLY, &other);
 nvs_set_str(handle, "password", "PRIVATE_VALUE");
 nvs_get_blob(other, make_key(slot, nested(1, 2)), PRIVATE_BUFFER, &PRIVATE_SIZE);
 nvs_erase_key(handle, "obsolete");
 nvs_erase_all(handle); nvs_commit(handle); nvs_close(other);
 nvs_get_stats("PRIVATE_PARTITION", &PRIVATE_STATS);`;
 const rows=scanNvsAccesses(source,'fixture.cpp');
 assert.equal(rows.length,8);
 assert.deepEqual(rows.map(row=>row.kind),['namespace','namespace','key','key','key','clear','commit','close']);
 assert.equal(rows[1].literal,'lxmf_ident');assert.equal(rows[1].handle,'&other');
 assert.equal(rows[3].expression,'make_key(slot, nested(1, 2))');assert.equal(rows[3].line,4);
 assert.ok(!/PRIVATE_/.test(JSON.stringify(rows)));
});
test('NVS audit excludes comment/string decoys and keeps multiline argument nesting',()=>{
 const source=`// nvs_set_blob(fake, "decoy", SECRET, 10);
 /* nvs_open("decoy", NVS_READONLY, &fake); */
 const char* text="nvs_close(fake)";
 nvs_get_u32(
   handle,
   key_for(slot /* , ) */, pair(1,2)),
   &SECRET);`;
 const rows=scanNvsAccesses(source,'fixture.h');assert.equal(rows.length,1);
 assert.equal(rows[0].line,4);assert.equal(rows[0].literal,null);assert.equal(rows[0].handle,'handle');
 assert.ok(!JSON.stringify(rows).includes('SECRET'));
});
test('authoritative aliases ignore comment/string decoys and reject incomplete or conflicting entries',()=>{
 const prefix='constexpr size_t kMaxNvsKeyLength = 15;';
 const table=body=>prefix+`constexpr StorageKeyAlias kStorageKeyAliases[] = {${body}};`;
 const source=`// constexpr StorageKeyAlias kStorageKeyAliases[] = {{"fake","wrong"}};
 const char* decoy="kMaxNvsKeyLength = 1";
 ${table('/* {"ghost","wrong"}, */ {"display_locale","disp_locale"}, {"wifi_sta_password","wifi_sta_pwd"},')}`;
 assert.deepEqual(scanNvsKeyAliases(source),{limit:15,entries:[{logical:'display_locale',stored:'disp_locale'},{logical:'wifi_sta_password',stored:'wifi_sta_pwd'}]});
 for(const body of ['{"same","a"},{"same","b"}', '{"a","same"},{"b","same"}', '{"a","1234567890123456"}', '{"a","b"},{key,"dynamic"}'])assert.throws(()=>scanNvsKeyAliases(table(body)));
 assert.throws(()=>scanNvsKeyAliases(prefix));
});
test('current NVS inventory and alias map are reproducible without updating documentation',async()=>{
 const root=fileURLToPath(new URL('../../',import.meta.url));
 const result=await runNvsAudit(root,{check:true});
 assert.ok(result.calls>0);assert.ok(result.sources.some(file=>file.endsWith('identity_atomic_record.h')));
 const source=await readFile(new URL('../../platform/esp/common/include/platform/esp/common/settings_store_impl.h',import.meta.url),'utf8');
 const aliases=scanNvsKeyAliases(source);assert.equal(result.aliases,aliases.entries.length);
 assert.equal(aliases.entries.find(row=>row.logical==='wifi_sta_password').stored,'wifi_sta_pwd');
});
