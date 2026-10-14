export function buildKmlTimeline(model){
 const tracks=[];let rejected=0;
 const walk=(geometry,feature)=>{
  if(geometry.geometries){geometry.geometries.forEach(child=>walk(child,feature));return;}
  if(geometry.type!=='Track'||!geometry.time?.length)return;
  if(geometry.time.length!==geometry.points.length){rejected++;return;}
  const indices=[];let previous=-Infinity,invalid=false;
  geometry.time.forEach((time,index)=>{if(time===null)return;if(!Number.isFinite(time)||time<previous)invalid=true;else{previous=time;indices.push(index);}});
  if(invalid){rejected++;return;}if(!indices.length)return;
  const start=geometry.time[indices[0]],end=geometry.time[indices.at(-1)];
  tracks.push({geometry,feature,indices,start,end,durationS:(end-start)/1000,missing:indices.length!==geometry.points.length});
 };
 model.features.forEach((feature,index)=>feature.geometries.forEach(geometry=>walk(geometry,index)));
 return {tracks,rejected};
}
// Select an actual recorded point. No spatial or timestamp interpolation,
// no extrapolated speed, and no implicit transition to another geometry.
export function pointAtKmlTime(track,timestamp){
 if(!Number.isFinite(timestamp)||!track?.indices.length)return null;
 let low=0,high=track.indices.length;
 while(low<high){const middle=(low+high)>>>1;if(track.geometry.time[track.indices[middle]]<=timestamp)low=middle+1;else high=middle;}
 const index=track.indices[Math.max(0,low-1)];
 return {point:track.geometry.points[index],time:track.geometry.time[index],index,feature:track.feature};
}
