import test from 'node:test';import assert from 'node:assert/strict';
import {buildKmlTimeline,pointAtKmlTime} from '../../site/tracks/src/kml-timeline.js';
import {kmlTimelineText} from '../../site/i18n/kml-timeline-labels.js';
import {locales} from '../../site/i18n/registry.js';
const track=times=>({type:'Track',time:times,points:times.map((_,index)=>({longitude:index,latitude:0,altitude:null}))});
const model=(...geometries)=>({features:[{geometries}]});
test('timeline selects only actual recorded points and preserves missing-time gaps',()=>{
 const geometry=track([1000,null,5000,9000]),data=model(geometry),before=JSON.stringify(data),timeline=buildKmlTimeline(data),row=timeline.tracks[0];
 assert.equal(row.missing,true);assert.deepEqual(row.indices,[0,2,3]);assert.equal(row.durationS,8);
 assert.equal(pointAtKmlTime(row,4000).point,geometry.points[0]);assert.equal(pointAtKmlTime(row,5000).index,2);assert.equal(pointAtKmlTime(row,8999).index,2);assert.equal(pointAtKmlTime(row,9000).index,3);assert.equal(pointAtKmlTime(row,0).index,0);assert.equal(pointAtKmlTime(row,99999).index,3);
 assert.equal(pointAtKmlTime(row,NaN),null);assert.equal(JSON.stringify(data),before);
});
test('segments stay separate even when their times overlap or are in a different global order',()=>{
 const first=track([5000,9000]),second=track([1000,6000]),timeline=buildKmlTimeline(model({type:'MultiTrack',geometries:[first,second]}));
 assert.equal(timeline.tracks.length,2);assert.equal(timeline.rejected,0);assert.equal(pointAtKmlTime(timeline.tracks[0],7000).point,first.points[0]);assert.equal(pointAtKmlTime(timeline.tracks[1],7000).point,second.points[1]);
});
test('bad order, nonfinite timestamps and mismatched counts cannot be replayed',()=>{
 const badCount=track([0,1000]);badCount.points.pop();const timeline=buildKmlTimeline(model(track([1000,0]),track([0,Infinity]),badCount,track([null,null]),{type:'LineString',points:[{},{}]}));
 assert.equal(timeline.tracks.length,0);assert.equal(timeline.rejected,3);assert.equal(pointAtKmlTime(null,0),null);
});
test('equal timestamps and one timed point are retained without inferred duration',()=>{
 const timeline=buildKmlTimeline(model(track([0,0,1000]),track([null,5000,null])));assert.equal(pointAtKmlTime(timeline.tracks[0],0).index,1);assert.equal(timeline.tracks[1].durationS,0);assert.equal(pointAtKmlTime(timeline.tracks[1],9999).index,1);
});
test('replay copy is complete in eleven languages',()=>{for(const {id} of locales){const text=kmlTimelineText(id);assert.equal(Object.keys(text).length,13);for(const value of Object.values(text))assert.ok(value.trim());}});
