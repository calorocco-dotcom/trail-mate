import test from 'node:test';
import assert from 'node:assert/strict';
import {readDeviceTrackCatalogPage} from '../../site/tracks/src/device-catalog.js';
const device=value=>({async request(operation,body){assert.equal(operation,80);return typeof value==='function'?value(body):value;}});
test('catalogue preserves names, exact sizes, recording state and cursor requests',async()=>{
 const page=await readDeviceTrackCatalogPage(device(body=>{assert.deepEqual(body,[null]);return [3,128,['山径.CSV',2,900,1]];}));
 assert.equal(page.entry.name,'山径.CSV');assert.equal(page.entry.bytes,900n);assert.equal(page.entry.recording,true);assert.deepEqual(page.cursor,{media:3,position:128});assert.ok(Object.isFrozen(page.entry)&&Object.isFrozen(page.cursor));
 const next=await readDeviceTrackCatalogPage(device(body=>{assert.deepEqual(body,[[3,128]]);return [3,160,['big.bin',3,1n<<60n,0]];}),page.cursor);assert.equal(next.entry.bytes,1n<<60n);
 assert.equal((await readDeviceTrackCatalogPage(device([3,null,null]),next.cursor)).cursor,null);
});
test('empty scan pages continue; changed media and non-progress reject',async()=>{
 assert.equal((await readDeviceTrackCatalogPage(device([1,128,null]))).entry,null);
 await assert.rejects(readDeviceTrackCatalogPage(device([2,160,null]),{media:1,position:128}),/会话已变化/);
 for(const next of [0,31,128,128*1024*1024+32])await assert.rejects(readDeviceTrackCatalogPage(device([1,next,null]),{media:1,position:128}),/游标未推进/);
});
test('catalogue rejects malformed rows, wrong extensions, control files and lossy sizes',async()=>{
 for(const row of [['../bad.gpx',1,2,0],['active.bin',3,2,0],['bad.csv',1,2,0],['ok.csv',2,-1,0],['ok.csv',2,Number.MAX_SAFE_INTEGER+1,0],['ok.csv',2,1n<<64n,0],['ok.csv',2,2,true],['x'.repeat(128)+'.gpx',1,2,0]])await assert.rejects(readDeviceTrackCatalogPage(device([1,32,row])));
 await assert.rejects(readDeviceTrackCatalogPage(device([1,null,['ok.csv',2,2,0]])),/结束标记/);
});
test('cancel rejects late page without accepting its data',async()=>{
 const abort=new AbortController();const session=device(()=>{abort.abort();return [1,32,['ok.csv',2,2,0]];});await assert.rejects(readDeviceTrackCatalogPage(session,null,{signal:abort.signal}),error=>error.name==='AbortError');
});
test('numeric management conflict/busy have catalogue-specific messages',async()=>{
 for(const [code,message] of [[4,/列表已变化/],[5,/稍后重试/]])await assert.rejects(readDeviceTrackCatalogPage({async request(){throw Object.assign(Error('management status'),{code});}}),message);
});
