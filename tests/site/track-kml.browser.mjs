import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {build} from '../../site/node_modules/esbuild/lib/main.js';
import assert from 'node:assert/strict';
const bundle=await build({stdin:{contents:"import {parseKml,descriptionContent} from './site/tracks/src/kml.js';globalThis.kmlTest={parseKml,descriptionContent};",resolveDir:process.cwd()},bundle:true,write:false,format:'iife',platform:'browser',target:'es2022'});
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
const page=await browser.newPage();const requests=[];page.on('request',r=>requests.push(r.url()));
try{
 await page.addScriptTag({content:bundle.outputFiles[0].text});
 const result=await page.evaluate(()=>{
   const {parseKml,descriptionContent}=globalThis.kmlTest;
   const xml=`<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:gx="http://www.google.com/kml/ext/2.2"><Document><name>路线合集</name><Style id="trail"><LineStyle><color>ff00ffff</color></LineStyle></Style><Folder><name>山脊</name><Placemark id="route"><name>去程与返回</name><styleUrl>#trail</styleUrl><MultiGeometry><LineString><altitudeMode>absolute</altitudeMode><coordinates>120,30,800 120.01,30.01,900</coordinates></LineString><LineString><coordinates>120.02,30.02 120.03,30.03</coordinates></LineString></MultiGeometry><ExtendedData><Data name="activity"><value>hiking</value></Data></ExtendedData></Placemark><Placemark id="waypoint"><name>观景台</name><Point><coordinates>120.01,30.01,900</coordinates></Point></Placemark><Placemark><gx:Track><when>2026-10-09T00:00:00Z</when><when>2026-10-09T00:01:00Z</when><gx:coord>120 30 800</gx:coord><gx:coord>120.01 30.01 900</gx:coord></gx:Track></Placemark><NetworkLink><name>不自动访问</name><Link><href>https://invalid.example/network.kml</href></Link></NetworkLink></Folder></Document></kml>`;
   const resourceXml=xml.replace('</Folder>','<GroundOverlay id="overlay"><name>Overlay image</name><Icon><href>https://invalid.example/map.png</href></Icon></GroundOverlay><Placemark><name>Model owner</name><Model id="model"><Link><href>models/terrain.dae</href></Link></Model></Placemark><gx:Tour id="tour"><name>Tour</name></gx:Tour></Folder>');
   const resources=parseKml(resourceXml).resources;
   let resourceBounded=false;try{parseKml(resourceXml.replace('</Folder>','<NetworkLink/><NetworkLink/></Folder>'),{limits:{bytes:100000,points:100000,features:4,elements:200000,depth:64}});}catch{resourceBounded=true;}
   const document=parseKml(xml),description=descriptionContent('<p>说明 <strong>保留</strong><img src="https://invalid.example/image.jpg" alt="路段照片"><script>window.bad=true</script><a href="javascript:alert(1)" onclick="bad()">链接</a></p>');
   const invalid=[xml.replace('120,30,800','181,30,800'),xml.replace('<kml','<!DOCTYPE kml><kml'),xml.replace('<when>2026-10-09T00:01:00Z</when>',''),xml.replace('120.01,30.01,900','not-a-coordinate')];
   const rejected=invalid.map(text=>{try{parseKml(text);return false;}catch{return true;}});
   let bounded=false;try{parseKml(xml,{limits:{bytes:100000,points:2,features:5000,elements:200000,depth:64}});}catch{bounded=true;}
   return {features:document.features.length,points:document.pointCount,segments:document.features[0].geometries[0].geometries.length,altitude:document.features[0].geometries[0].geometries[0].altitudeMode,extension:document.features[0].extendedData[0].value,time:document.features[2].geometries[0].time,styles:document.styles.size,preserved:document.issues.some(x=>x.element==='NetworkLink'),original:new TextDecoder().decode(document.source)===xml,description,rejected,bounded,resources,resourceBounded,resourceOriginal:new TextDecoder().decode(parseKml(resourceXml).source)===resourceXml};
 });
 assert.equal(result.features,3);assert.equal(result.points,7);assert.equal(result.segments,2);assert.equal(result.altitude,'absolute');assert.equal(result.extension,'hiking');assert.equal(result.time[1]-result.time[0],60000);assert.equal(result.styles,1);assert.equal(result.preserved,true);assert.equal(result.original,true);assert.ok(result.rejected.every(Boolean));assert.equal(result.bounded,true);
 assert.ok(result.description.html.includes('<strong>保留</strong>'));assert.ok(!result.description.html.includes('javascript:'));assert.ok(!result.description.html.includes('onclick'));assert.ok(!result.description.html.includes('<img'));assert.equal(result.description.images.length,1);
 assert.deepEqual(requests,[]);
 assert.equal(result.resourceOriginal,true);assert.equal(result.resourceBounded,true);
 assert.deepEqual(result.resources.map(item=>item.type),['NetworkLink','GroundOverlay','Model','gx:Tour']);
 assert.equal(result.resources[1].id,'overlay');assert.equal(result.resources[1].href,'https://invalid.example/map.png');
 assert.deepEqual(result.resources[2].path,['路线合集','山脊','Model owner']);assert.equal(result.resources[2].href,'models/terrain.dae');
 console.log('Browser KML tests passed: segments, layers, waypoint, time, altitude, styles, extensions, original preservation and inert descriptions.');
}finally{await browser.close();}
