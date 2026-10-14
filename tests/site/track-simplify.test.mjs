import test from 'node:test';import assert from 'node:assert/strict';
import {simplifyKmlPoints} from '../../site/tracks/src/kml-simplify-points.js';
import {kmlSimplifyText} from '../../site/i18n/kml-simplify-labels.js';
import {locales} from '../../site/i18n/registry.js';
const p=(longitude,latitude=0,altitude=null)=>({longitude,latitude,altitude});
test('straight lines simplify without mutating original points and keep endpoints',()=>{
 const points=Array.from({length:100000},(_,i)=>p(i/100000)),before=JSON.stringify(points),result=simplifyKmlPoints(points,5);assert.deepEqual(result.indices,[0,99999]);assert.equal(JSON.stringify(points),before);assert.ok(result.checks<2000000);
});
test('corners, extrema, missing boundaries and protected point anchors survive',()=>{
 assert.deepEqual(simplifyKmlPoints([p(0),p(.01,.01),p(.02)],1).indices,[0,1,2]);
 const points=[p(0,0,0),p(.01,0,100),p(.02,0,null),p(.03,0,null),p(.04,0,-10),p(.05,0,0)];assert.deepEqual(simplifyKmlPoints(points,100).indices,[0,1,2,3,4,5]);
 assert.deepEqual(simplifyKmlPoints([p(0),p(.01),p(.02),p(.03)],100,{anchors:[1]}).indices,[0,1,3]);
 assert.deepEqual(simplifyKmlPoints([p(0),p(.01),p(.02),p(.03)],100,{time:[0,null,2000,3000]}).indices,[0,1,2,3]);
});
test('date-line and polar coordinates use spherical geometry, including degenerate edges',()=>{
 assert.deepEqual(simplifyKmlPoints([p(179.9),p(180),p(-179.9)],5).indices,[0,2]);
 assert.deepEqual(simplifyKmlPoints([p(0,90),p(90,90),p(180,90)],1).indices,[0,2]);
 assert.deepEqual(simplifyKmlPoints([p(0),p(0),p(0)],1).indices,[0,2]);
 const result=simplifyKmlPoints([p(0),p(90),p(180)],1);assert.deepEqual(result.indices,[0,1,2]);
});
test('budget and malformed inputs fail instead of returning an unbounded or unverified result',()=>{
 assert.throws(()=>simplifyKmlPoints([p(0),p(.01),p(.02),p(.03)],1,{maxChecks:1}),{code:'SIMPLIFY_BUDGET'});
 for(const tolerance of [0,NaN,10001])assert.throws(()=>simplifyKmlPoints([p(0),p(1)],tolerance),{code:'SIMPLIFY_INPUT'});
 assert.throws(()=>simplifyKmlPoints([p(0),p(1)],1,{time:[0]}),{code:'SIMPLIFY_INPUT'});
});
test('simplification copy is complete across all supported languages',()=>{for(const {id} of locales){const text=kmlSimplifyText(id);assert.equal(Object.keys(text).length,12);for(const value of Object.values(text))assert.ok(value.trim());}});
