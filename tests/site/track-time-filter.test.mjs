import test from 'node:test';
import assert from 'node:assert/strict';
import {parseKmlTimeValue,formatKmlTimeInput,filterKmlTimeGeometry,kmlTimeMatches,kmlTimeFeatureVisible,kmlProfilePointVisible,collectKmlTimeScopes} from '../../site/tracks/src/kml-time.js';
import {kmlTimeFilterText,kmlTimeFilterKeys} from '../../site/i18n/kml-time-filter-labels.js';
import {locales} from '../../site/i18n/registry.js';
test('calendar precision, leap dates, 24-hour boundaries and timezone offsets are explicit and strict',()=>{
 const stamp=raw=>parseKmlTimeValue(raw).start;
 assert.deepEqual(parseKmlTimeValue('2024'),{start:Date.UTC(2024,0,1),end:Date.UTC(2025,0,1)-1,precision:'year'});
 assert.deepEqual(parseKmlTimeValue('2024-02'),{start:Date.UTC(2024,1,1),end:Date.UTC(2024,2,1)-1,precision:'month'});
 assert.deepEqual(parseKmlTimeValue('2024-02-29'),{start:Date.UTC(2024,1,29),end:Date.UTC(2024,2,1)-1,precision:'day'});
 assert.equal(stamp('2024-02-29T24:00:00Z'),Date.UTC(2024,2,1));assert.equal(stamp('2026-01-01T08:00:00+08:00'),stamp('2026-01-01T00:00:00Z'));
 assert.equal(stamp('2026-01-01T00:00:00'),stamp('2026-01-01T00:00:00Z'));assert.equal(stamp('2026-01-01T00:00:00.123456Z'),Date.UTC(2026,0,1)+123);
 assert.equal(parseKmlTimeValue('2026-01+08:00').start,Date.UTC(2026,0,1)-8*3600000);
 for(const raw of ['', '2023-02-29','2026-02-30','2026-13','0000','02026','2026-01-01T24:00:01Z','2026-01-01T01:00:00+14:01','2026-01-01T01:00Z','2026-01-01T01:00:60Z','1'.repeat(64),'NaN','2026\u00a0'])assert.throws(()=>parseKmlTimeValue(raw));
 assert.throws(()=>parseKmlTimeValue('2026',{full:true}));
 for(const raw of ['0001-01-01T00:00:00Z','-0001-02-29T00:00:00Z','-0002-01-01T00:00:00Z','10000-01-01T00:00:00Z','2026-01-01T00:00:00.123Z'])assert.equal(stamp(formatKmlTimeInput(stamp(raw))),stamp(raw));
});
test('point clipping preserves actual coordinates, splits unknown/outside gaps, and never interpolates',()=>{
 const points=Array.from({length:7},(_,longitude)=>({longitude,latitude:0,altitude:longitude})),geometry={type:'Track',points,time:[0,1000,null,3000,4000,5000,6000]},before=JSON.stringify(geometry);
 assert.equal(filterKmlTimeGeometry(geometry,null),geometry);
 const result=filterKmlTimeGeometry(geometry,{start:1000,end:5000});assert.equal(result.type,'MultiGeometry');assert.deepEqual(result.geometries.map(row=>[row.type,row.points.map(point=>point.longitude)]),[['Point',[1]],['Track',[3,4,5]]]);assert.equal(result.geometries[1].points[0],points[3]);
 assert.equal(filterKmlTimeGeometry(geometry,{start:1500,end:2500}),null);assert.equal(filterKmlTimeGeometry({...geometry,time:[]},{start:1500,end:2500}).points,points);assert.equal(JSON.stringify(geometry),before);
 const multi={type:'MultiTrack',geometries:[geometry,{type:'Track',points:[points[0]],time:[10000]}]},clipped=filterKmlTimeGeometry(multi,{start:0,end:1000});assert.equal(clipped.geometries.length,1);assert.deepEqual(clipped.geometries[0].geometries[0].time,[0,1000]);
});
test('visible-feature counts, profile markers and open intervals use the same time predicate',()=>{
 const scopes={features:[{visible:true,time:{start:1000,end:2000},geometries:[{type:'Point',points:[{}]}]},{visible:false,time:null,geometries:[{}]},{visible:true,time:null,geometries:[{type:'Track',points:[{}],time:[3000]}]}]},range={start:0,end:1500};
 assert.equal(kmlTimeFeatureVisible(scopes.features[0],range),true);assert.equal(kmlTimeFeatureVisible(scopes.features[1],range),false);assert.equal(kmlTimeFeatureVisible(scopes.features[2],range),false);
 assert.equal(kmlProfilePointVisible({feature:0,time:1000},scopes,range),true);assert.equal(kmlProfilePointVisible({feature:0,time:2000},scopes,range),false);assert.equal(kmlProfilePointVisible({feature:1},scopes,range),false);
 scopes.profileSegments=[{type:'Track',time:[0,null,3000]}];assert.equal(kmlProfilePointVisible({feature:0,segment:0,index:2},scopes,range),false);assert.equal(kmlProfilePointVisible({feature:0,segment:0,index:1},scopes,range),false);assert.equal(kmlProfilePointVisible({feature:0,segment:0,index:0},scopes,range),true);
 assert.equal(kmlTimeMatches({start:-Infinity,end:1000},range),true);assert.equal(kmlTimeMatches({invalid:true},range),false);assert.equal(kmlTimeMatches({invalid:true},null),true);assert.equal(kmlTimeMatches(null,range),true);
 const plain={features:[{visible:true,geometries:[{type:'Track',points:[{},{}],time:[0,5000]}]}]};assert.equal(collectKmlTimeScopes(plain).minimum,0);assert.equal(collectKmlTimeScopes(plain).maximum,5000);
});
test('eleven languages explain preview-only time policy and empty/invalid cases',()=>{for(const {id} of locales){const text=kmlTimeFilterText(id);assert.deepEqual(Object.keys(text),kmlTimeFilterKeys);for(const value of Object.values(text))assert.ok(value.trim());}});
