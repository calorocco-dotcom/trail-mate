import test from 'node:test';import assert from 'node:assert/strict';
import {photoViewUvBounds,photoTilePlan} from '../../site/tracks/src/kml-photo-pyramid.js';
import {photoProjectionUv} from '../../site/tracks/src/kml-photo-preview.js';
const photo={shape:'sphere',rotation:0,volume:{leftFov:-180,rightFov:180,bottomFov:-90,topFov:90,near:1},href:'tiles/$[level]/$[x]_$[y].png',pyramid:{tileSize:256,maxWidth:32768,maxHeight:16384,gridOrigin:'lowerLeft'}};
test('Adaptive pyramid planning requests bounded exact tiles and more detail for zoom',()=>{
 const full=photoTilePlan(photo,{yaw:0,pitch:0,fov:100,width:960,height:540}),zoom=photoTilePlan(photo,{yaw:0,pitch:0,fov:10,width:960,height:540});assert.ok(zoom.level>full.level);assert.ok(full.refs.length<=16&&zoom.refs.length<=16);assert.ok(zoom.width*zoom.height<=4000000);assert.ok(zoom.width<=2048&&zoom.height<=2048);assert.equal(new Set(zoom.refs).size,zoom.refs.length);for(const tile of zoom.tiles)assert.equal(tile.reference,`tiles/${zoom.level}/${tile.x}_${tile.y}.png`);
 const reduced=photoTilePlan(photo,{fov:100,width:960,height:540},{maxTiles:1,maxPixels:65536,maxTextureSize:256});assert.equal(reduced.tiles.length,1);assert.equal(reduced.level,0);assert.equal(reduced.qualityLimited,true);
});
test('Conservative UV window contains every visible ray for all shapes, rotations and asymmetric views',()=>{
 for(const shape of ['rectangle','cylinder','sphere'])for(const rotation of [0,30,-90])for(const yaw of [-30,0,35])for(const pitch of [-15,10]){
  const p={...photo,shape,rotation,volume:{leftFov:shape==='rectangle'?-70:-180,rightFov:shape==='rectangle'?80:180,bottomFov:shape==='sphere'?-90:-70,topFov:shape==='sphere'?90:80,near:2}},view={yaw,pitch,fov:35,width:960,height:540},bounds=photoViewUvBounds(p,view),r=Math.PI/180,t=Math.tan(view.fov*r/2);
  for(let y=-1;y<=1;y+=.1)for(let x=-1;x<=1;x+=.1){const a=x*(view.width/view.height)*t,b=y*t,c=1,py=Math.cos(pitch*r)*b+Math.sin(pitch*r)*c,pz=-Math.sin(pitch*r)*b+Math.cos(pitch*r)*c,ray=[Math.cos(yaw*r)*a+Math.sin(yaw*r)*pz,py,-Math.sin(yaw*r)*a+Math.cos(yaw*r)*pz],uv=photoProjectionUv(p,ray);if(!uv)continue;assert.ok(bounds);assert.ok(uv[0]>=bounds[0]-1e-9&&uv[0]<=bounds[2]+1e-9&&uv[1]>=bounds[1]-1e-9&&uv[1]<=bounds[3]+1e-9,JSON.stringify({shape,rotation,yaw,pitch,bounds,uv}));}
 }
 assert.deepEqual(photoViewUvBounds(photo,{yaw:179,pitch:0,fov:60}),[0,0,1,1]);assert.deepEqual(photoViewUvBounds(photo,{yaw:0,pitch:85,fov:20}),[0,0,1,1]);
});
test('Grid origin, rectangular non-power-of-two image padding and tile atlas coordinates stay exact',()=>{
 const p={...photo,pyramid:{tileSize:256,maxWidth:1000,maxHeight:700,gridOrigin:'lowerLeft'}},lower=photoTilePlan(p,{yaw:-30,pitch:30,fov:20,width:800,height:500}),upper=photoTilePlan({...p,pyramid:{...p.pyramid,gridOrigin:'upperLeft'}},{yaw:-30,pitch:30,fov:20,width:800,height:500});assert.equal(lower.level,upper.level);assert.equal(lower.width,upper.width);assert.ok(lower.rect[0]>=0&&lower.rect[2]>=lower.rect[0]);for(const plan of [lower,upper])for(const tile of plan.tiles){assert.ok(tile.left>=0&&tile.left+256<=plan.width);assert.ok(tile.top>=0&&tile.top+256<=plan.height);}
 assert.notDeepEqual(lower.tiles.map(t=>t.y),upper.tiles.map(t=>t.y));
});
test('Bad templates, dimensions and impossible budgets fail before reading any file',()=>{
 for(const changed of [{...photo,href:'remote.png'},{...photo,pyramid:{...photo.pyramid,tileSize:300}},{...photo,pyramid:{...photo.pyramid,maxWidth:NaN}}])assert.throws(()=>photoTilePlan(changed,{fov:60}));assert.throws(()=>photoTilePlan(photo,{fov:60},{maxPixels:1}));assert.throws(()=>photoTilePlan(photo,{fov:180}));const simple={...photo,href:'one.png',pyramid:{tileSize:256,maxWidth:200,maxHeight:120,gridOrigin:'upperLeft'}};assert.deepEqual(photoTilePlan(simple,{fov:60}).refs,['one.png']);
});
