function profileDistance(a,b){
 const radians=Math.PI/180,lat=(b.latitude-a.latitude)*radians,lon=(b.longitude-a.longitude)*radians;
 const h=Math.sin(lat/2)**2+Math.cos(a.latitude*radians)*Math.cos(b.latitude*radians)*Math.sin(lon/2)**2;
 return 6371008.8*2*Math.atan2(Math.sqrt(Math.min(1,h)),Math.sqrt(Math.max(0,1-h)));
}
export function buildKmlElevationProfile(model,maxSamples=2048){
 if(!Number.isInteger(maxSamples)||maxSamples<4||maxSamples>4096)throw Error('Invalid elevation sample budget');
 const segments=[];
 const walk=(geometry,feature)=>{if(geometry.geometries)geometry.geometries.forEach(child=>walk(child,feature));else if(['LineString','Track'].includes(geometry.type)&&geometry.points?.length>1)segments.push({geometry,feature});};
 model.features.forEach((feature,index)=>feature.geometries.forEach(geometry=>walk(geometry,index)));
 const count=segments.reduce((total,{geometry})=>total+geometry.points.length,0),bucketSize=Math.max(1,Math.ceil(count/Math.floor(maxSamples/4)));
 let distanceM=0,minAltitude=Infinity,maxAltitude=-Infinity,eligible=0,order=0,group=0,bucket=null;
 const samples=[];
 const flush=()=>{if(!bucket)return;const unique=new Map([bucket.first,bucket.min,bucket.max,bucket.last].map(point=>[point.order,point]));samples.push(...[...unique.values()].sort((a,b)=>a.order-b.order));bucket=null;};
 segments.forEach(({geometry,feature},segment)=>{
  group++;
  geometry.points.forEach((point,index)=>{
   if(index)distanceM+=profileDistance(geometry.points[index-1],point);
   if(geometry.altitudeMode==='absolute'&&Number.isFinite(point.altitude)){
    const sample={point,feature,segment,index,order,group,distanceM,altitude:point.altitude};eligible++;
    minAltitude=Math.min(minAltitude,point.altitude);maxAltitude=Math.max(maxAltitude,point.altitude);
    if(!bucket)bucket={first:sample,min:sample,max:sample,last:sample};
    else{bucket.last=sample;if(sample.altitude<bucket.min.altitude)bucket.min=sample;if(sample.altitude>bucket.max.altitude)bucket.max=sample;}
   }else group++; // Missing/relative/ground heights are not sea-level elevation.
   order++;if(order%bucketSize===0)flush();
  });
 });flush();
 return {samples,distanceM,eligible,minAltitude:eligible?minAltitude:null,maxAltitude:eligible?maxAltitude:null,sampled:eligible>samples.length};
}
