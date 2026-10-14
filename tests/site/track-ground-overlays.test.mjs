import test from 'node:test';import assert from 'node:assert/strict';
import {groundOverlayQuad,groundOverlayBox,overlayHomography} from '../../site/tracks/src/kml-ground-overlays.js';import {locales} from '../../site/i18n/registry.js';import {kmlGroundOverlayText,formatGroundOverlayIssues} from '../../site/i18n/kml-ground-overlay-labels.js';
test('quad tuples are counterclockwise, convex, image-corner ordered and date-line unwrapped',()=>{
 assert.deepEqual(groundOverlayQuad('179,10 -179,10 -179,12 179,12'),[{longitude:179,latitude:12},{longitude:181,latitude:12},{longitude:181,latitude:10},{longitude:179,latitude:10}]);
 assert.equal(groundOverlayQuad('0,0 .0000001,0 .0000001,.0000001 0,.0000001').length,4);
 for(const input of ['0,0 0,1 1,1 1,0','0,0 1,1 1,0 0,1','0,0 1,0 1,1','0,0 0,0 1,1 0,1','0,0 1,0 .2,.1 0,1','181,0 182,0 182,1 181,1','NaN,0 1,0 1,1 0,1'])assert.throws(()=>groundOverlayQuad(input));
});
test('box bounds support date-line and extended longitudes with counterclockwise rotation',()=>{
 const box={north:2,south:0,west:179,east:-179};assert.deepEqual(groundOverlayBox(box).map(p=>p.longitude),[179,181,181,179]);assert.deepEqual(groundOverlayBox({...box,east:181}),groundOverlayBox(box));
 const rotated=groundOverlayBox({north:2,south:0,west:0,east:2,rotation:90}),expected=[[0,0],[0,2],[2,2],[2,0]];rotated.forEach((p,i)=>{assert.ok(Math.abs(p.longitude-expected[i][0])<1e-12);assert.ok(Math.abs(p.latitude-expected[i][1])<1e-12);});
 for(const invalid of [{north:0},{south:3},{east:179},{rotation:181},{west:1e100}])assert.throws(()=>groundOverlayBox({...box,...invalid}));
});
test('projective mapping reaches all four corners without treating a trapezoid as an affine rectangle',()=>{
 for(const corners of [[{x:50,y:20},{x:550,y:20},{x:550,y:300},{x:50,y:300}],[{x:1e7,y:2e7},{x:1e7+500,y:2e7+25},{x:1e7+420,y:2e7+350},{x:1e7-80,y:2e7+280}]]){const {h,origin}=overlayHomography(corners),uv=[[0,0],[1,0],[1,1],[0,1]];for(let i=0;i<4;i++){const [u,v]=uv[i],d=h[6]*u+h[7]*v+1;assert.ok(Math.abs(origin.x+(h[0]*u+h[1]*v+h[2])/d-corners[i].x)<1e-7);assert.ok(Math.abs(origin.y+(h[3]*u+h[4]*v+h[5])/d-corners[i].y)<1e-7);}}
 assert.equal(overlayHomography([{x:0,y:0},{x:0,y:0},{x:0,y:0},{x:0,y:0}]),null);assert.equal(overlayHomography([{x:NaN,y:0},{x:0,y:0},{x:0,y:0},{x:0,y:0}]),null);
});
test('all overlay limitations are readable in every supported language',()=>{for(const {id} of locales){const text=kmlGroundOverlayText(id);assert.equal(Object.keys(text).length,7);assert.ok(Object.values(text).every(value=>value.trim()));assert.deepEqual(formatGroundOverlayIssues([{code:'missing',name:'Layer'},{code:'missing',name:'Layer'}],id),['Layer · '+text.missing]);}});
