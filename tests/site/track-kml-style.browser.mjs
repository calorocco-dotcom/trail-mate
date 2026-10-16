import {chromium} from '../../site/node_modules/playwright/index.mjs';
import http from 'node:http';
import fs from 'node:fs/promises';
import assert from 'node:assert/strict';
const sources=new Map(await Promise.all(['kml','kml-time','track-datetime','kml-style','kml-icon-crop'].map(async name=>['/'+name+'.js',await fs.readFile(`site/tracks/src/${name}.js`,'utf8')])));
sources.set('/i18n/kml-style-labels.js',await fs.readFile('site/i18n/kml-style-labels.js','utf8'));
sources.set('/i18n/kml-crop-labels.js',await fs.readFile('site/i18n/kml-crop-labels.js','utf8'));
const server=http.createServer((req,res)=>{if(req.url==='/'){res.setHeader('Content-Type','text/html');res.end('<!doctype html><title>KML styles</title>');}else if(sources.has(req.url)){res.setHeader('Content-Type','text/javascript');res.end(sources.get(req.url));}else res.writeHead(404).end();});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${server.address().port}`;
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true}),page=await browser.newPage();const requests=[];page.on('request',request=>requests.push(request.url()));
try{
 await page.goto(origin+'/');
 const result=await page.evaluate(async()=>{
  const {parseKml}=await import('/kml.js'),{createKmlStyleResolver,kmlColor}=await import('/kml-style.js');
  const input=`<kml xmlns="http://www.opengis.net/kml/2.2"><Document>
  <Style id="normal"><LineStyle><color>800000ff</color><width>4</width></LineStyle><PolyStyle><color>4000ff00</color><fill>0</fill><outline>0</outline></PolyStyle></Style>
  <Style id="highlight"><LineStyle><color>ff00ff00</color><width>7</width></LineStyle></Style>
  <StyleMap id="map"><Pair><key>normal</key><styleUrl>#normal</styleUrl></Pair><Pair><key>highlight</key><styleUrl>#highlight</styleUrl></Pair></StyleMap>
  <StyleMap id="cycle"><Pair><key>normal</key><styleUrl>#cycle</styleUrl></Pair></StyleMap>
  <Style id="duplicate"/><Style id="duplicate"/>
  <Placemark id="first"><styleUrl>#map</styleUrl><Style><LineStyle><width>9</width></LineStyle></Style><Point><coordinates>120,30</coordinates></Point></Placemark>
  <Placemark><styleUrl>#cycle</styleUrl><Point><coordinates>120,30</coordinates></Point></Placemark>
  <Placemark><styleUrl>#duplicate</styleUrl><Point><coordinates>120,30</coordinates></Point></Placemark>
  <Placemark><styleUrl>https://invalid.example/style.kml#x</styleUrl><Point><coordinates>120,30</coordinates></Point></Placemark>
  <Placemark><Style><IconStyle><color>ff332211</color><scale>2</scale><Icon><href>https://invalid.example/icon.png</href></Icon></IconStyle><LabelStyle><color>ff998877</color><scale>0</scale></LabelStyle></Style><Point><coordinates>120,30</coordinates></Point></Placemark>
  <Placemark id="random"><Style><LineStyle><color>80402010</color><colorMode>random</colorMode><width>100000</width></LineStyle></Style><Point><coordinates>120,30</coordinates></Point></Placemark>
  </Document></kml>`;
  const document=parseKml(input),resolve=createKmlStyleResolver(document),normal=resolve(0),highlight=resolve(0,{highlight:true});
  const random=resolve(5),again=resolve(5);
  return {normal,highlight,cycle:resolve(1).issues,duplicate:resolve(2).issues,external:resolve(3).issues,icon:resolve(4),random,again,
   rgba:kmlColor('ff332211'),unchanged:new TextDecoder().decode(document.source)===input};
 });
 assert.deepEqual(result.rgba,{color:'#112233',opacity:1});assert.equal(result.normal.line.color,'#ff0000');assert.equal(result.normal.line.opacity,128/255);assert.equal(result.normal.line.weight,9);
 assert.equal(result.highlight.line.color,'#00ff00');assert.equal(result.highlight.line.weight,9);assert.equal(result.normal.polygon.fillColor,'#00ff00');assert.equal(result.normal.polygon.fillOpacity,64/255);
 assert.equal(result.normal.polygon.fill,false);assert.equal(result.normal.polygon.stroke,false);
 assert.ok(result.cycle.some(issue=>issue.includes('循环')));assert.ok(result.duplicate.some(issue=>issue.includes('重复')));assert.ok(result.external.some(issue=>issue.includes('外部')));
 assert.equal(result.icon.point.color,'#112233');assert.equal(result.icon.point.radius,14);assert.equal(result.icon.label.scale,0);assert.ok(result.icon.issues.some(issue=>issue.includes('图标')));
 assert.deepEqual(result.random,result.again);assert.equal(result.random.line.weight,32);assert.equal(result.random.line.opacity,128/255);
 const rgb=result.random.line.color.match(/[0-9a-f]{2}/g).map(value=>parseInt(value,16));assert.ok(rgb[0]<=16&&rgb[1]<=32&&rgb[2]<=64);
 assert.equal(result.unchanged,true);assert.ok(requests.every(url=>url.startsWith(origin)));
 console.log('KML style resolution passed: AABBGGRR alpha, StyleMap states, inline precedence, polygon flags, point/label styles, random mode, cycles, duplicates and no external loads.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
