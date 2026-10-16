const R=6371008.8,rad=Math.PI/180;
export function simplifyKmlPoints(points,tolerance,{anchors=[],maxChecks=2000000,time=[]}={}){
 const fail=code=>{throw Object.assign(Error(code),{code});};
 if(!Array.isArray(points)||points.length<2||points.length>100000||!Number.isFinite(tolerance)||tolerance<.1||tolerance>10000||!Number.isInteger(maxChecks)||maxChecks<1||maxChecks>2000000||!Array.isArray(time)||time.length&&time.length!==points.length||time.some(value=>value!==null&&!Number.isFinite(value)))fail('SIMPLIFY_INPUT');
 const keep=new Uint8Array(points.length);keep[0]=keep[points.length-1]=1;let minimum=-1,maximum=-1;
 points.forEach((point,index)=>{
  if(!Number.isFinite(point.latitude)||Math.abs(point.latitude)>90||!Number.isFinite(point.longitude)||Math.abs(point.longitude)>180||point.altitude!==null&&!Number.isFinite(point.altitude))fail('SIMPLIFY_INPUT');
  if(point.altitude!==null){if(minimum<0||point.altitude<points[minimum].altitude)minimum=index;if(maximum<0||point.altitude>points[maximum].altitude)maximum=index;}
  if(index&&((point.altitude===null)!==(points[index-1].altitude===null)||time.length&&((time[index]===null)!==(time[index-1]===null))))keep[index]=keep[index-1]=1;
 });if(minimum>=0)keep[minimum]=keep[maximum]=1;
 for(const index of anchors){if(!Number.isInteger(index)||index<0||index>=points.length)fail('SIMPLIFY_INPUT');keep[index]=1;}
 const angle=(a,b)=>{const lat=(b.latitude-a.latitude)*rad,lon=(b.longitude-a.longitude)*rad,h=Math.sin(lat/2)**2+Math.cos(a.latitude*rad)*Math.cos(b.latitude*rad)*Math.sin(lon/2)**2;return 2*Math.atan2(Math.sqrt(Math.min(1,h)),Math.sqrt(Math.max(0,1-h)));};
 const bearing=(a,b)=>Math.atan2(Math.sin((b.longitude-a.longitude)*rad)*Math.cos(b.latitude*rad),Math.cos(a.latitude*rad)*Math.sin(b.latitude*rad)-Math.sin(a.latitude*rad)*Math.cos(b.latitude*rad)*Math.cos((b.longitude-a.longitude)*rad));
 const retained=[];for(let i=0;i<keep.length;i++)if(keep[i])retained.push(i);
 const pending=[];for(let i=1;i<retained.length;i++)pending.push([retained[i-1],retained[i]]);let checks=0;
 while(pending.length){
  const [first,last]=pending.pop();if(last-first<2)continue;const a=points[first],b=points[last],length=angle(a,b),direction=bearing(a,b);let index=-1,largest=tolerance;
  if(length>Math.PI-1e-7)index=(first+last)>>>1; // No unique short arc for antipodal endpoints.
  else for(let i=first+1;i<last;i++){
   if(++checks>maxChecks)fail('SIMPLIFY_BUDGET');const p=points[i],ap=angle(a,p),delta=bearing(a,p)-direction,along=Math.atan2(Math.sin(ap)*Math.cos(delta),Math.cos(ap));
   const distance=(length<1e-12||along<0||along>length?Math.min(ap,angle(b,p)):Math.asin(Math.min(1,Math.abs(Math.sin(ap)*Math.sin(delta)))))*R;
   if(distance>largest){largest=distance;index=i;}
  }
  if(index>=0){keep[index]=1;pending.push([first,index],[index,last]);}
 }
 const indices=[];for(let i=0;i<keep.length;i++)if(keep[i])indices.push(i);return {indices,checks};
}
