import {validTrackDateTime} from './track-datetime.js';
// Signed index bounds use a constant-memory anchor arc, distinct from preview
// minimum-arc bounds. Coordinates quantize to E7 by rounding toward +infinity
// on half ties. XML parsing/validation remains the KML parser's responsibility.
export function manifestGeometryBounds(document){
 let anchor,west,east,south,north,points=0;
 const wrap=value=>value>=1800000000?value-3600000000:value< -1800000000?value+3600000000:value;
 function add(point){
  if(!Number.isFinite(point.longitude)||!Number.isFinite(point.latitude)||Math.abs(point.longitude)>180||Math.abs(point.latitude)>90||++points>100000)throw Error('轨迹索引坐标无效或超预算');
  let x=wrap(Math.round(point.longitude*1e7)),y=Math.round(point.latitude*1e7);
  if(anchor===undefined){anchor=west=east=x;south=north=y;}
  else{let delta=x-anchor;if(delta>=1800000000)delta-=3600000000;if(delta< -1800000000)delta+=3600000000;x=anchor+delta;west=Math.min(west,x);east=Math.max(east,x);south=Math.min(south,y);north=Math.max(north,y);}
 }
 function walk(geometry){
  if(geometry.type==='Track'){
   if(!Array.isArray(geometry.rawTime)||geometry.rawTime.length&&geometry.rawTime.length!==geometry.points?.length||geometry.rawTime.some(text=>!validTrackDateTime(text,{allowUnknown:true})))throw Error('轨迹时间不符合设备共享规则，请检查原始 KML 时间项');
  }
  if(geometry.geometries)geometry.geometries.forEach(walk);else if(geometry.rings)geometry.rings.forEach(walk);else if(geometry.points)geometry.points.forEach(add);
 }
 document.features.forEach(feature=>feature.geometries.forEach(walk));if(!points)throw Error('轨迹索引没有坐标');
 return {bbox:[wrap(west),south,wrap(east),north],pointCount:points};
}
