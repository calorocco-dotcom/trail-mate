import test from 'node:test';import assert from 'node:assert/strict';
import {buildKmlElevationProfile} from '../../site/tracks/src/kml-elevation.js';
import {configElevationText} from '../../site/i18n/kml-elevation-labels.js';
import {locales} from '../../site/i18n/registry.js';
const point=(longitude,altitude,latitude=0)=>({longitude,latitude,altitude});
const line=(points,altitudeMode='absolute',type='LineString')=>({type,points,altitudeMode});
const model=(...geometries)=>({features:[{geometries}]});
test('profile keeps independent segments and excludes missing or nonabsolute heights',()=>{
 const data=model({type:'MultiGeometry',geometries:[line([point(0,100),point(.01,null),point(.02,200)]),line([point(10,300),point(10.01,250)]),line([point(11,9000),point(11.01,10000)],'relativeToGround'),{type:'Point',points:[point(12,80000)]}]});
 const before=JSON.stringify(data),result=buildKmlElevationProfile(data);
 assert.equal(result.eligible,4);assert.equal(result.minAltitude,100);assert.equal(result.maxAltitude,300);assert.equal(result.samples.length,4);
 assert.notEqual(result.samples[0].group,result.samples[1].group);assert.notEqual(result.samples[1].group,result.samples[2].group);assert.equal(result.samples[2].group,result.samples[3].group);
 assert.ok(result.distanceM>4400&&result.distanceM<4500,'distance must not bridge segment boundaries');assert.equal(JSON.stringify(data),before);
});
test('bounded display sampling retains extrema, endpoints and original point references',()=>{
 const points=Array.from({length:200000},(_,index)=>point(index*.00001,100));points[45231].altitude=9000;points[179221].altitude=-50;
 const result=buildKmlElevationProfile(model(line(points)),2048);assert.ok(result.samples.length<=2048);assert.equal(result.sampled,true);assert.equal(result.eligible,200000);
 assert.equal(result.minAltitude,-50);assert.equal(result.maxAltitude,9000);assert.ok(result.samples.some(sample=>sample.point===points[45231]));assert.ok(result.samples.some(sample=>sample.point===points[179221]));assert.equal(result.samples[0].point,points[0]);assert.equal(result.samples.at(-1).point,points.at(-1));
 for(let i=1;i<result.samples.length;i++)assert.ok(result.samples[i].order>result.samples[i-1].order);
});
test('date line distance is short and zero/negative absolute altitudes remain valid',()=>{
 const result=buildKmlElevationProfile(model(line([point(179.9,0),point(-179.9,-5)],'absolute','Track')));assert.equal(result.minAltitude,-5);assert.equal(result.maxAltitude,0);assert.ok(result.distanceM>22000&&result.distanceM<23000);
 for(const mode of ['clampToGround','relativeToGround','relativeToSeaFloor','clampToSeaFloor']){const empty=buildKmlElevationProfile(model(line([point(0,100),point(.01,200)],mode)));assert.equal(empty.minAltitude,null);assert.equal(empty.eligible,0);}
 assert.throws(()=>buildKmlElevationProfile(model(),3));assert.throws(()=>buildKmlElevationProfile(model(),4097));
});
test('elevation controls have complete copy in every supported language',()=>{for(const {id} of locales){const text=configElevationText(id);assert.equal(Object.keys(text).length,9);for(const value of Object.values(text))assert.ok(value.trim());}});
