// Browser preview statistics; the device must independently validate shared data.
const radians=value=>value*Math.PI/180;
function distance(a,b){
 const lat=radians(b.latitude-a.latitude),lon=radians(b.longitude-a.longitude);
 const h=Math.sin(lat/2)**2+Math.cos(radians(a.latitude))*Math.cos(radians(b.latitude))*Math.sin(lon/2)**2;
 return 6371008.8*2*Math.atan2(Math.sqrt(Math.min(1,h)),Math.sqrt(Math.max(0,1-h)));
}
export function summarizeKml(document){
 const points=[],segments=[];
 function walk(geometry){
  if(geometry.geometries)geometry.geometries.forEach(walk);
  else if(geometry.rings)geometry.rings.forEach(walk);
  else if(geometry.points){for(const point of geometry.points)points.push(point);if(['LineString','Track'].includes(geometry.type)&&geometry.points.length>1)segments.push(geometry);}
 }
 document.features.forEach(feature=>feature.geometries.forEach(walk));
 let distanceM=0,ascentM=0,absolute=segments.length>0,timed=segments.length>0,minTime=Infinity,maxTime=-Infinity;
 for(const segment of segments){
  const heights=segment.altitudeMode==='absolute'&&segment.points.every(p=>p.altitude!==null);absolute&&=heights;
  const times=segment.time;
  if(!times||times.length!==segment.points.length||times.some((value,i)=>value===null||!Number.isFinite(value)||i&&value<times[i-1]))timed=false;
  else{minTime=Math.min(minTime,times[0]);maxTime=Math.max(maxTime,times.at(-1));}
  for(let i=1;i<segment.points.length;i++){distanceM+=distance(segment.points[i-1],segment.points[i]);if(heights)ascentM+=Math.max(0,segment.points[i].altitude-segment.points[i-1].altitude);}
 }
 let bounds=null;
 if(points.length){
  const longitude=points.map(p=>(p.longitude+360)%360).sort((a,b)=>a-b);let gap=-1,start=0,end=0;
  for(let i=0;i<longitude.length;i++){const next=longitude[(i+1)%longitude.length]+(i===longitude.length-1?360:0),size=next-longitude[i];if(size>gap){gap=size;start=next%360;end=longitude[i];}}
  const wrap=n=>n>180?n-360:n;
  let south=90,north=-90;for(const point of points){south=Math.min(south,point.latitude);north=Math.max(north,point.latitude);}
  bounds={west:wrap(start),south,east:wrap(end),north,crossesDateLine:wrap(start)>wrap(end)};
 }
 return {featureCount:document.features.length,pointCount:document.pointCount,segments:segments.length,
  distanceM:segments.length?distanceM:null,ascentM:absolute?ascentM:null,durationS:timed?(maxTime-minTime)/1000:null,bounds};
}
