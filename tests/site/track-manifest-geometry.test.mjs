import test from 'node:test';
import assert from 'node:assert/strict';
import {manifestGeometryBounds} from '../../site/tracks/src/manifest-geometry.js';
const model=points=>({features:[{geometries:[{points}]}]});
test('signed bounds retain dateline, points and nested geometry',()=>{
 const document={features:[{geometries:[{geometries:[{points:[{longitude:179,latitude:1}]},{rings:[{points:[{longitude:-179,latitude:-2}]}]}]}]}]};
 assert.deepEqual(manifestGeometryBounds(document),{bbox:[1790000000,-20000000,-1790000000,10000000],pointCount:2});
 assert.deepEqual(manifestGeometryBounds(model([{longitude:180,latitude:90}])).bbox,[-1800000000,900000000,-1800000000,900000000]);
});
test('empty, invalid and excess point inputs reject',()=>{assert.throws(()=>manifestGeometryBounds(model([])));assert.throws(()=>manifestGeometryBounds(model([{longitude:181,latitude:0}])));assert.throws(()=>manifestGeometryBounds(model(Array(100001).fill({longitude:0,latitude:0}))));});

test('sharing index validates original Track dates without Date normalization',()=>{
 const document={features:[{geometries:[{type:'Track',points:[{longitude:1,latitude:2}],rawTime:['2024-02-29T00:00:00Z']}]}]};
 const track=document.features[0].geometries[0];
 assert.equal(manifestGeometryBounds(document).pointCount,1);
 for(const text of ['2023-02-29T00:00:00Z','2024-02-29','2024-01-01T00:00:00+14:01']){track.rawTime=[text];assert.throws(()=>manifestGeometryBounds(document),/时间/);}
 for(const values of [[],[''],['2024-01-01T24:00:00Z']]){track.rawTime=values;assert.equal(manifestGeometryBounds(document).pointCount,1);}
 track.rawTime=['',''];assert.throws(()=>manifestGeometryBounds(document),/时间/);
 delete track.rawTime;assert.throws(()=>manifestGeometryBounds(document),/时间/);
});
