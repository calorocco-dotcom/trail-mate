import {chromium} from '../../site/node_modules/playwright/index.mjs';
import http from 'node:http';
import fs from 'node:fs/promises';
import assert from 'node:assert/strict';
const sources=new Map(await Promise.all(['kml','kml-time','track-datetime','kml-edit','kml-summary','kml-line-operations','kml-track-operations','kml-segment-operations','kml-simplify','kml-simplify-points'].map(async name=>['/'+name+'.js',await fs.readFile(`site/tracks/src/${name}.js`,'utf8')])));
const server=http.createServer((req,res)=>{if(req.url==='/'){res.setHeader('Content-Type','text/html');res.end('<!doctype html><title>KML editing test</title>');}
 else if(sources.has(req.url)){res.setHeader('Content-Type','text/javascript');res.end(sources.get(req.url));}else res.writeHead(404).end();});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true}),page=await browser.newPage();
const requests=[];page.on('request',request=>requests.push(request.url()));
try{
 await page.goto(`http://127.0.0.1:${server.address().port}/`);
 const result=await page.evaluate(async()=>{
  const {parseKml}=await import('/kml.js'),edit=await import('/kml-edit.js'),{summarizeKml}=await import('/kml-summary.js'),{transformKmlLine}=await import('/kml-line-operations.js'),{transformKmlTrack}=await import('/kml-track-operations.js'),{transformKmlSegments}=await import('/kml-segment-operations.js'),{simplifyKmlGeometry}=await import('/kml-simplify.js');
  const source=`<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:gx="http://www.google.com/kml/ext/2.2" xmlns:x="urn:retained"><Document><name>Original</name><Style id="s"><LineStyle><color>ff123456</color></LineStyle></Style><Folder><name>Segments</name><Placemark id="route"><name>Route</name><styleUrl>#s</styleUrl><MultiGeometry><LineString><altitudeMode>absolute</altitudeMode><coordinates>179.9,30,100 -179.9,30,120</coordinates></LineString><LineString><altitudeMode>absolute</altitudeMode><coordinates>170,30,300 170.01,30,310</coordinates></LineString></MultiGeometry><ExtendedData><Data name="original"><value>keep</value></Data></ExtendedData><x:payload value="kept"/></Placemark><Placemark id="timed"><gx:Track><altitudeMode>absolute</altitudeMode><when>2026-10-10T00:00:00Z</when><when>2026-10-10T00:01:00Z</when><gx:coord>179.9 30 100</gx:coord><gx:coord>-179.9 30 120</gx:coord><gx:angles>0 0 0</gx:angles><gx:angles>1 0 0</gx:angles></gx:Track></Placemark><NetworkLink><Link><href>https://invalid.example/never</href></Link></NetworkLink></Folder></Document></kml>`;
  const original=parseKml(source),summary=summarizeKml(original);
  const named=edit.editKmlFeature(source,0,{name:'Edited <route>',description:'<p>Text</p>',visible:false});
  const namedDocument=parseKml(named);
  const changed=edit.editKmlCoordinates(named,0,[0,0],[{longitude:179.8,latitude:30,altitude:105},{longitude:-179.8,latitude:30,altitude:125}]);
  const timed=edit.editKmlCoordinates(changed,1,[0],[{longitude:179.7,latitude:30,altitude:100},{longitude:-179.7,latitude:30,altitude:120}]);
  const added=edit.addKmlWaypoint(timed,{id:'new-waypoint',name:'Viewpoint',longitude:179.95,latitude:30.01,altitude:900});
  const removed=edit.removeKmlFeature(added,2),metadata=edit.editKmlMetadata(removed,{name:'Updated document',description:'Document text'});
  const after=parseKml(metadata),serialized=new TextDecoder().decode(metadata);
  const invalid=[()=>edit.editKmlCoordinates(source,1,[0],[{longitude:1,latitude:1,altitude:0}]),
   ()=>edit.addKmlWaypoint(source,{id:'route',longitude:0,latitude:0}),()=>edit.addKmlWaypoint(source,{longitude:181,latitude:0}),
   ()=>edit.editKmlCoordinates(source,0,[0,0],[{longitude:0,latitude:0}]),()=>edit.editKmlFeature(source,99,{name:'x'})];
  const rejected=invalid.map(run=>{try{run();return false;}catch{return true;}});
  const onlyTime=parseKml('<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:gx="http://www.google.com/kml/ext/2.2"><Placemark><gx:Track><when>2026-10-10T00:00:00Z</when><when>2026-10-10T00:01:00Z</when><gx:coord>179.9 30 100</gx:coord><gx:coord>-179.9 30 120</gx:coord></gx:Track></Placemark></kml>');
  const timeSummary=summarizeKml(onlyTime);
  const large=parseKml('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>'+Array(100000).fill('120,30').join(' ')+'</coordinates></LineString></Placemark></kml>');
  const largeSummary=summarizeKml(large);
  const operationSource='<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:x="urn:keep"><Document><Style id="s"><LineStyle><color>ff123456</color></LineStyle></Style><Placemark id="route"><styleUrl>#s</styleUrl><LineString id="original-line"><altitudeMode>absolute</altitudeMode><extrude>1</extrude><coordinates>120,30,10 120.01,30,20 120.02,30,30 120.03,30,40</coordinates><x:keep>retained</x:keep></LineString></Placemark></Document></kml>';
  const reversed=parseKml(transformKmlLine(operationSource,0,[0],{action:'reverse'})),cropped=parseKml(transformKmlLine(operationSource,0,[0],{action:'crop',start:1,end:2})),splitSource=transformKmlLine(operationSource,0,[0],{action:'split',start:1}),splitModel=parseKml(splitSource),group=splitModel.features[0].geometries[0];
  if(reversed.features[0].geometries[0].points[0].longitude!==120.03||cropped.pointCount!==2||group.geometries.length!==2||group.geometries[0].points.length!==2||group.geometries[1].points.length!==3)throw Error('Line operation geometry mismatch');
  if(group.id!=='original-line'||splitModel.xml.getElementsByTagNameNS('urn:keep','keep').length!==2||splitModel.styles.size!==1||splitModel.features[0].styleUrl!=='#s'||!group.geometries.every(g=>g.altitudeMode==='absolute'&&g.extrude))throw Error('Line properties or extensions lost');
  if(Math.abs(summarizeKml(splitModel).distanceM-summarizeKml(parseKml(operationSource)).distanceM)>0.000001)throw Error('Split acquired a connecting edge');
  const operationRejected=[()=>transformKmlLine(source,1,[0],{action:'reverse'}),()=>transformKmlLine(operationSource,0,[0],{action:'crop',start:2,end:2}),()=>transformKmlLine(operationSource,0,[0],{action:'split',start:0}),()=>transformKmlLine(operationSource.replace('<x:keep>','<x:keep id="unknown-id">'),0,[0],{action:'split',start:1})].map(run=>{try{run();return false;}catch{return true;}});
  if(edit.kmlGeometryEditors(operationSource,0)[0].splitSafe!==true||edit.kmlGeometryEditors(operationSource.replace('<x:keep>','<x:keep id="unknown-id">'),0)[0].splitSafe!==false)throw Error('Split compatibility missing');
  const trackSource='<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:g="http://www.google.com/kml/ext/2.2" xmlns:x="urn:keep"><Placemark><name>Timed pairs</name><g:Track id="paired"><altitudeMode>absolute</altitudeMode>'+Array.from({length:4},(_,i)=>`<when>2026-10-10T00:0${i}:00Z</when><g:coord>${120+i/100} 30 ${10+i}</g:coord><g:angles>${i} 0 0</g:angles>`).join('')+'<ExtendedData><Data name="source"><value>preserved</value></Data><SchemaData schemaUrl="#samples"><g:SimpleArrayData name="sensor"><g:value>a</g:value><g:value>b</g:value><g:value>c</g:value><g:value>d</g:value></g:SimpleArrayData></SchemaData></ExtendedData></g:Track></Placemark></kml>';
  const croppedTrack=parseKml(transformKmlTrack(trackSource,0,[0],{action:'crop',start:1,end:2})),splitTrack=parseKml(transformKmlTrack(trackSource,0,[0],{action:'split',start:1}));
  const gx='http://www.google.com/kml/ext/2.2',values=xml=>[...xml.getElementsByTagNameNS(gx,'value')].map(node=>node.textContent);
  if(croppedTrack.pointCount!==2||croppedTrack.features[0].geometries[0].rawTime.join(',')!=='2026-10-10T00:01:00Z,2026-10-10T00:02:00Z'||values(croppedTrack.xml).join(',')!=='b,c'||[...croppedTrack.xml.getElementsByTagNameNS(gx,'angles')].map(node=>node.textContent).join(',')!=='1 0 0,2 0 0')throw Error('Timed crop pairing lost');
  const paired=splitTrack.features[0].geometries[0];
  if(paired.type!=='MultiTrack'||paired.id!=='paired'||paired.interpolate||paired.geometries[0].rawTime.length!==2||paired.geometries[1].rawTime.length!==3||values(splitTrack.xml).join(',')!=='a,b,b,c,d'||splitTrack.xml.getElementsByTagNameNS(gx,'angles').length!==5)throw Error('Timed split pairing lost');
  if(!edit.kmlGeometryEditors(trackSource,0)[0].sequenceSafe||edit.kmlGeometryEditors(trackSource.replace('<g:value>d</g:value>',''),0)[0].sequenceSafe||edit.kmlGeometryEditors(trackSource.replace('</g:Track>','<x:opaque>preserved</x:opaque></g:Track>'),0)[0].sequenceSafe)throw Error('Timed compatibility incorrect');
  const timedRejected=[()=>transformKmlTrack(trackSource,0,[0],{action:'reverse'}),()=>transformKmlTrack(trackSource.replace('<g:value>d</g:value>',''),0,[0],{action:'crop',start:1,end:2}),()=>transformKmlTrack(trackSource.replace('<g:angles>3 0 0</g:angles>',''),0,[0],{action:'crop',start:1,end:2}),()=>transformKmlTrack(trackSource.replace('</g:Track>','<x:opaque>preserved</x:opaque></g:Track>'),0,[0],{action:'crop',start:1,end:2})].map(run=>{try{run();return false;}catch{return true;}});
  const segmentSource='<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:x="urn:keep"><Placemark><MultiGeometry><LineString id="first"><altitudeMode>absolute</altitudeMode><coordinates>120,30,10 120.01,30,20</coordinates></LineString><Point><coordinates>119,30</coordinates></Point><LineString><altitudeMode>absolute</altitudeMode><coordinates>120.01,30,20 120.02,30,30</coordinates></LineString><LineString id="third"><altitudeMode>absolute</altitudeMode><coordinates>120.02,30,30 120.03,30,40</coordinates></LineString></MultiGeometry></Placemark></kml>';
  const later=transformKmlSegments(segmentSource,0,[0,0],{action:'later'}),laterModel=parseKml(later.source),merged=transformKmlSegments(segmentSource,0,[0,0],{action:'merge'}),mergedModel=parseKml(merged.source);
  if(later.geometryPath.join(',')!=='0,2'||laterModel.features[0].geometries[0].geometries[2].id!=='first'||mergedModel.features[0].geometries[0].geometries[0].points.length!==3||mergedModel.features[0].geometries[0].geometries[0].id!=='first'||mergedModel.features[0].geometries[0].geometries.length!==3||Math.abs(summarizeKml(mergedModel).distanceM-summarizeKml(parseKml(segmentSource)).distanceM)>0.000001)throw Error('Segment move/merge mismatch');
  const mergeRejected=[()=>transformKmlSegments(segmentSource.replace('<LineString>','<LineString id="second">'),0,[0,0],{action:'merge'}),()=>transformKmlSegments(segmentSource.replace('120.01,30,20 120.02','120.015,30,20 120.02'),0,[0,0],{action:'merge'}),()=>transformKmlSegments(segmentSource.replace('120.01,30,20 120.02,30,30</coordinates>','120.01,30,20 120.02,30,30</coordinates><x:opaque>keep</x:opaque>'),0,[0,0],{action:'merge'}),()=>transformKmlSegments(segmentSource,0,[0,1],{action:'later'})].map(run=>{try{run();return false;}catch{return true;}});
  const datelineSegments='<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><MultiGeometry><LineString><coordinates>179.9,30,10 180,30,20</coordinates></LineString><LineString><coordinates>-180,30,20 -179.9,30,30</coordinates></LineString></MultiGeometry></Placemark></kml>',datelineMerged=parseKml(transformKmlSegments(datelineSegments,0,[0,0],{action:'merge'}).source);
  if(datelineMerged.pointCount!==3||Math.abs(summarizeKml(datelineMerged).distanceM-summarizeKml(parseKml(datelineSegments)).distanceM)>0.000001)throw Error('Date-line endpoint merge changed distance');
  const simplifiedTrack=simplifyKmlGeometry(trackSource,0,[0],100),simpleModel=parseKml(simplifiedTrack.source);
  if(simplifiedTrack.after!==2||values(simpleModel.xml).join(',')!=='a,d'||[...simpleModel.xml.getElementsByTagNameNS(gx,'angles')].map(node=>node.textContent).join(',')!=='0 0 0,3 0 0'||simpleModel.features[0].geometries[0].rawTime.join(',')!=='2026-10-10T00:00:00Z,2026-10-10T00:03:00Z')throw Error('Simplification pairing lost');
  const protectedSimplification=simplifyKmlGeometry(trackSource.replace('<g:value>b</g:value>','<g:value id="protected-point">b</g:value>'),0,[0],100);if(protectedSimplification.after!==3)throw Error('Referenced point removed');
  const simplifyRejected=[()=>simplifyKmlGeometry(trackSource.replace('</g:Track>','<x:opaque>keep</x:opaque></g:Track>'),0,[0],100),()=>simplifyKmlGeometry(operationSource,0,[0],100)].map(run=>{try{run();return false;}catch{return true;}});
  return {original:new TextDecoder().decode(original.source)===source,name:namedDocument.features[0].name,visible:namedDocument.features[0].visible,
   description:namedDocument.features[0].description,features:after.features.length,metadata:after.tree[0].name,
   time:after.features[1].geometries[0].time,retained:serialized.includes('ff123456')&&serialized.includes('value="kept"')&&serialized.includes('https://invalid.example/never')&&serialized.includes('gx:angles'),
   summary,timeSummary,largeSummary,rejected,operationRejected,timedRejected,mergeRejected,simplifyRejected};
 });
 assert.equal(result.original,true);assert.equal(result.name,'Edited <route>');assert.equal(result.visible,false);assert.equal(result.description,'<p>Text</p>');
 assert.equal(result.features,2);assert.equal(result.metadata,'Updated document');assert.equal(result.retained,true);assert.ok(result.rejected.every(Boolean));
 assert.equal(result.time[1]-result.time[0],60000);assert.equal(result.summary.segments,3);assert.ok(result.summary.distanceM<50000,'separate segments acquired a connecting edge');
 assert.equal(result.summary.ascentM,50);assert.equal(result.summary.durationS,null);
 assert.equal(result.timeSummary.bounds.crossesDateLine,true);assert.equal(result.timeSummary.durationS,60);assert.equal(result.timeSummary.ascentM,null);
 assert.equal(result.largeSummary.pointCount,100000);assert.equal(result.largeSummary.distanceM,0);
 assert.ok(result.operationRejected.every(Boolean));
 assert.ok(result.timedRejected.every(Boolean));
 assert.ok(result.mergeRejected.every(Boolean));
 assert.ok(result.simplifyRejected.every(Boolean));
 assert.ok(requests.every(url=>url.startsWith(`http://127.0.0.1:${server.address().port}/`)));
 console.log('KML editor and statistics passed: preserving styles/extensions/time, segments, dateline, mutations, invalid edits, 100000 points and no remote requests.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
