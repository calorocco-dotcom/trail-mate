import test from 'node:test';
import assert from 'node:assert/strict';
import {zipSync,unzipSync} from '../../site/node_modules/fflate/esm/index.mjs';
import {originalFormat,originalFilenameMatches} from '../../site/tracks/src/original-format.js';
import {exportDraftBackup,readDraftBackup,validateDraftBackupSnapshot} from '../../site/tracks/src/draft-backup.js';
import {recordingOriginalText} from '../../site/i18n/recording-original-labels.js';
import {locales} from '../../site/i18n/registry.js';
const utf8=new TextEncoder(),source=utf8.encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Document/></kml>');
function fixture(format){return{source,name:'记录',filename:'record.kml',originalSource:new Uint8Array([0,1,255,13,10]),archiveMember:'原记录.'+format,originalFormat:format};}
test('typed recording backups retain raw bytes and original name without pretending they are KML',async()=>{
 for(const format of ['gpx','csv','bin']){
  const input=fixture(format),zip=await exportDraftBackup(input),files=unzipSync(zip.bytes),restored=await readDraftBackup(zip.bytes);
  assert.equal(zip.info.version,2);assert.equal(zip.info.originalFormat,format);assert.deepEqual(files['original.'+format],input.originalSource);assert.equal(files['original.kml'],undefined);assert.deepEqual(files['draft.kml'],source);
  assert.equal(restored.originalFormat,format);assert.equal(restored.archiveMember,input.archiveMember);assert.deepEqual(restored.originalSource,input.originalSource);assert.deepEqual(restored.source,source);assert.equal(restored.attachmentRows.length,0);
 }
});
test('ordinary KML and no-original backups remain version one and round trip',async()=>{
 for(const input of [{source,name:'普通',filename:'track.kml'},{source,name:'普通',filename:'track.kml',originalSource:source,archiveMember:'original.kml'}]){
  const backup=await exportDraftBackup(input),restored=await readDraftBackup(backup.bytes);assert.equal(backup.info.version,1);assert.equal('originalFormat' in backup.info,false);assert.equal(restored.originalFormat,undefined);assert.deepEqual(restored.originalSource,input.originalSource);
 }
});
test('recording backup rejects invalid formats, wrong names, missing originals, changed bytes and mislabeled members',async()=>{
 const input=fixture('gpx'),backup=await exportDraftBackup(input),files=unzipSync(backup.bytes),rewrite=mutate=>{const info=JSON.parse(new TextDecoder().decode(files['draft.json']));mutate(info);return zipSync({...files,'draft.json':utf8.encode(JSON.stringify(info))});};
 for(const format of ['exe','constructor','__proto__',{},null])await assert.rejects(exportDraftBackup({...input,originalFormat:format}));
 await assert.rejects(exportDraftBackup({...input,archiveMember:'false.kml'}));await assert.rejects(exportDraftBackup({...input,originalSource:undefined}));
 for(const change of [info=>info.originalFormat='csv',info=>info.originalFormat='kml',info=>info.files.originalSource=null,info=>info.version=1,info=>info.files.originalSource.path='original.kml',info=>info.originalFormat='../escape'])await assert.rejects(readDraftBackup(rewrite(change)));
 const bytes=files['original.gpx'].slice();bytes[0]^=1;await assert.rejects(readDraftBackup(zipSync({...files,'original.gpx':bytes})));
});
test('snapshot captures format, name and raw bytes before asynchronous validation',async()=>{
 const input=fixture('csv'),copy=input.originalSource.slice(),pending=validateDraftBackupSnapshot(input);input.originalSource.fill(9);input.originalFormat='bin';input.archiveMember='late.bin';const value=await pending;assert.equal(value.originalFormat,'csv');assert.equal(value.archiveMember,'原记录.csv');assert.deepEqual(value.originalSource,copy);
});
test('recording original controls have complete distinct translations in eleven languages',()=>{
 for(const {id} of locales){const copy=recordingOriginalText(id);assert.ok(copy.download.trim());assert.ok(copy.help.trim());assert.notEqual(copy.help,copy.download);}
 assert.throws(()=>recordingOriginalText('missing'));assert.equal(originalFormat().mime,'application/vnd.google-earth.kml+xml');assert.equal(originalFormat('bin').mime,'application/octet-stream');assert.equal(originalFilenameMatches('录制.CSV','csv'),true);assert.equal(originalFilenameMatches('录制.csv.kml','csv'),false);assert.throws(()=>originalFormat('constructor'));
});
