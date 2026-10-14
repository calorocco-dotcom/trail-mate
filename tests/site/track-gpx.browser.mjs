import {chromium} from '../../site/node_modules/playwright/index.mjs';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import assert from 'node:assert/strict';
const root=path.resolve('site'),server=http.createServer(async(req,res)=>{try{const pathname=new URL(req.url,'http://localhost').pathname;const filename=path.resolve(root,'.'+pathname+(pathname.endsWith('/')?'index.html':''));if(!filename.startsWith(root+path.sep))throw Error('path');const bytes=await fs.readFile(filename);res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.png':'image/png'})[path.extname(filename)]??'application/octet-stream');res.end(bytes);}catch{res.writeHead(404).end();}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true}),page=await browser.newPage();
const xml=`<gpx xmlns="http://www.topografix.com/GPX/1/1" xmlns:x="urn:extension" version="1.1" creator="test"><metadata><name>多段 GPX</name><link href="https://invalid.example/metadata"/></metadata><wpt lat="30" lon="120"><name>营地</name></wpt><rte><name>规划路线</name><rtept lat="30" lon="120"/><rtept lat="30.1" lon="120.1"/></rte><trk><name>实录</name><trkseg><trkpt lat="31" lon="121"><ele>100</ele><time>2026-10-11T08:00:00+08:00</time><extensions><x:heart>80</x:heart></extensions></trkpt><trkpt lat="31.1" lon="121.1"><ele>120</ele><time>2026-10-11T00:01:00Z</time></trkpt></trkseg><trkseg><trkpt lat="32" lon="122"><time>2026-10-11T00:02:00Z</time></trkpt><trkpt lat="32.1" lon="122.1"/></trkseg></trk></gpx>`;
try{
 await page.goto(`http://127.0.0.1:${server.address().port}/tracks/?lang=zh-Hans`);
 const result=await page.evaluate(async({xml})=>{
  const {convertGpx}=await import('/tracks/src/gpx-convert.js'),{parseKml}=await import('/tracks/src/kml.js');
  const input=new TextEncoder().encode(xml),converted=convertGpx(input),model=parseKml(converted.kml);
  const invalid=[xml.replace('lat="30"','lat="91"'),xml.replace('version="1.1"','version="1.0"'),xml.replace('2026-10-11T00:01:00Z','2026-02-31T00:01:00Z'),xml.replace('2026-10-11T00:01:00Z','2026-10-11T00:01:00'),xml.replace('<gpx','<!DOCTYPE gpx><gpx')];
  const rejected=invalid.map(text=>{try{convertGpx(new TextEncoder().encode(text));return false;}catch{return true;}});
  const legacy='<gpx version="1.1" creator="Trail Mate"><trk><name>Trail Mate Track</name><trkseg><trkpt lat="30.0000000" lon="120.0000000"><ele>100.00</ele><time>2026-10-11T00:00:00Z</time></trkpt></trkseg></trk></gpx>';
  const legacyConverted=convertGpx(new TextEncoder().encode(legacy));
  let unknownLegacyRejected=false;try{convertGpx(new TextEncoder().encode(legacy.replace('creator="Trail Mate"','creator="unknown"')));}catch{unknownLegacyRejected=true;}
  return {points:model.pointCount,features:model.features.length,types:model.features[2].geometries[0].geometries.map(g=>g.type),time:model.features[2].geometries[0].geometries[0].time[0],unknown:model.features[2].geometries[0].geometries[1].points[0].altitude,extension:new TextDecoder().decode(converted.kml).includes('heart'),same:new TextDecoder().decode(converted.source)===xml,rejected,legacyPoints:parseKml(legacyConverted.kml).pointCount,legacyWarning:legacyConverted.issues.some(issue=>issue.includes('命名空间')),linuxPoints:convertGpx(new TextEncoder().encode(legacy.replace('creator="Trail Mate"','creator="Trail Mate Linux"'))).pointCount,unknownLegacyRejected};
 },{xml});
 assert.equal(result.points,7);assert.equal(result.features,3);assert.deepEqual(result.types,['Track','LineString']);assert.equal(result.time,Date.parse('2026-10-11T00:00:00Z'));assert.equal(result.unknown,null);assert.equal(result.extension,true);assert.equal(result.same,true);assert.ok(result.rejected.every(Boolean));
 assert.equal(result.legacyPoints,1);assert.equal(result.linuxPoints,1);assert.equal(result.legacyWarning,true);assert.equal(result.unknownLegacyRejected,true);
 await page.locator('input[type=file][accept*=".gpx"]').setInputFiles({name:'segments.gpx',mimeType:'application/gpx+xml',buffer:Buffer.from(xml)});
 await page.getByText(/设备记录已转换为 KML 浏览器草稿/).waitFor();assert.equal(await page.locator('.tracks-row').count(),1);
 const pending=page.waitForEvent('download');await page.getByRole('button',{name:'下载 KML',exact:true}).click();const downloaded=await pending;assert.equal(downloaded.suggestedFilename(),'segments.kml');assert.ok((await fs.readFile(await downloaded.path(),'utf8')).includes('<MultiGeometry>'));
 await page.reload();await page.locator('.tracks-row').click();await page.getByText(/已打开浏览器草稿/).waitFor();
 console.log('GPX conversion and actual page passed: segments, waypoint, route, timezone, unknown altitude, extensions, invalid input, download and persisted draft.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
