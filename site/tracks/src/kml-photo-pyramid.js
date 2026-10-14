const radians=Math.PI/180,clamp=(value,a,b)=>Math.max(a,Math.min(b,value));
const fail=code=>{throw Object.assign(Error(code),{code:'KML_PHOTO_'+code});};
// A circumscribed angular cone contains the rectangular view frustum. Its
// latitude/longitude bounds are conservative, including poles and the seam.
// This prevents sampled ray grids from missing a thin visible photo sliver.
export function photoViewUvBounds(photo,{yaw=0,pitch=0,fov=60,width=960,height=540}={}){
 if(![yaw,pitch,fov,width,height].every(Number.isFinite)||width<=0||height<=0||fov<=0||fov>=180||Math.abs(pitch)>90||photo.invalid||!photo.volume)fail('INVALID');
 yaw=((yaw+180)%360+360)%360-180;const radius=Math.atan(Math.tan(fov*radians/2)*Math.hypot(width/height,1))/radians,v=photo.volume;
 if(Math.abs(pitch)+radius>=90)return [0,0,1,1];const delta=Math.asin(clamp(Math.sin(radius*radians)/Math.cos(pitch*radians),-1,1))/radians;
 const left=yaw-delta,right=yaw+delta,bottom=pitch-radius,top=pitch+radius;
 if(left< -180||right>180||photo.shape==='rectangle'&&(left<=-90||right>=90))return [0,0,1,1];
 let xs,ys;
 if(photo.shape==='rectangle'){xs=[left,right].map(angle=>(Math.tan(angle*radians)-Math.tan(v.leftFov*radians))/(Math.tan(v.rightFov*radians)-Math.tan(v.leftFov*radians)));ys=[bottom,top].flatMap(angle=>[left,right,clamp(0,left,right)].map(horizontal=>(Math.tan(angle*radians)/Math.cos(horizontal*radians)-Math.tan(v.bottomFov*radians))/(Math.tan(v.topFov*radians)-Math.tan(v.bottomFov*radians))));}
 else{xs=[left,right].map(angle=>(angle-v.leftFov)/(v.rightFov-v.leftFov));ys=[bottom,top].map(angle=>photo.shape==='sphere'?(angle-v.bottomFov)/(v.topFov-v.bottomFov):(Math.tan(angle*radians)-Math.tan(v.bottomFov*radians))/(Math.tan(v.topFov*radians)-Math.tan(v.bottomFov*radians)));}
 const angle=(photo.rotation??0)*radians,c=Math.cos(angle),s=Math.sin(angle),corners=[Math.min(...xs),Math.max(...xs)].flatMap(x=>[Math.min(...ys),Math.max(...ys)].map(y=>[.5+c*(x-.5)+s*(y-.5),.5-s*(x-.5)+c*(y-.5)]));
 const bounds=[Math.max(0,Math.min(...corners.map(p=>p[0]))),Math.max(0,Math.min(...corners.map(p=>p[1]))),Math.min(1,Math.max(...corners.map(p=>p[0]))),Math.min(1,Math.max(...corners.map(p=>p[1])))];return bounds[0]>=bounds[2]||bounds[1]>=bounds[3]?null:bounds;
}
export function photoTilePlan(photo,view,{maxTiles=16,maxPixels=4000000,maxTextureSize=2048}={}){
 const p=photo.pyramid;if(!p||!['lowerLeft','upperLeft'].includes(p.gridOrigin)||[p.tileSize,p.maxWidth,p.maxHeight].some(n=>!Number.isInteger(n)||n<1||n>2147483647)||!Number.isInteger(Math.log2(p.tileSize)))fail('INVALID');
 if(!Number.isInteger(maxTiles)||maxTiles<1||maxTiles>64||!Number.isFinite(maxPixels)||maxPixels<1||maxPixels>8000000||!Number.isInteger(maxTextureSize)||maxTextureSize<1||maxTextureSize>16384)fail('BUDGET');
 const bounds=photoViewUvBounds(photo,view),maxLevel=Math.max(0,Math.ceil(Math.log2(Math.max(p.maxWidth,p.maxHeight)/p.tileSize)));
 if(maxLevel>0&&!['level','x','y'].every(key=>photo.href?.includes('$['+key+']')))fail('TEMPLATE');
 if(!bounds)return {visible:false,refs:[],tiles:[]};
 const width=view?.width??960,height=view?.height??540,density=Math.max(width/((bounds[2]-bounds[0])*p.maxWidth),height/((bounds[3]-bounds[1])*p.maxHeight)),desired=clamp(maxLevel+Math.ceil(Math.log2(density)),0,maxLevel);
 for(let level=desired;level>=0;level--){
  const scale=2**(maxLevel-level),realWidth=p.maxWidth/scale,realHeight=p.maxHeight/scale,size=p.tileSize,cols=Math.ceil(realWidth/size),rows=Math.ceil(realHeight/size);
  const x0=clamp(Math.floor(bounds[0]*realWidth/size),0,cols-1),x1=clamp(Math.floor((bounds[2]*realWidth-1e-7)/size),0,cols-1),y0=clamp(Math.floor((p.gridOrigin==='upperLeft'?1-bounds[3]:bounds[1])*realHeight/size),0,rows-1),y1=clamp(Math.floor(((p.gridOrigin==='upperLeft'?1-bounds[1]:bounds[3])*realHeight-1e-7)/size),0,rows-1);
  const atlasWidth=(x1-x0+1)*size,atlasHeight=(y1-y0+1)*size,count=(x1-x0+1)*(y1-y0+1);
  if(count>maxTiles||atlasWidth>maxTextureSize||atlasHeight>maxTextureSize||atlasWidth*atlasHeight>maxPixels)continue;
  const tiles=[];for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){const reference=photo.href.replaceAll('$[level]',String(level)).replaceAll('$[x]',String(x)).replaceAll('$[y]',String(y));if(/\$\[[^\]]+\]/.test(reference))fail('TEMPLATE');tiles.push({level,x,y,reference,left:(x-x0)*size,top:(p.gridOrigin==='upperLeft'?y-y0:y1-y)*size});}
  const rect=[x0*size/realWidth,p.gridOrigin==='upperLeft'?1-(y1+1)*size/realHeight:y0*size/realHeight,(x1+1)*size/realWidth,p.gridOrigin==='upperLeft'?1-y0*size/realHeight:(y1+1)*size/realHeight];
  return {visible:true,level,maxLevel,desiredLevel:desired,qualityLimited:level<desired,width:atlasWidth,height:atlasHeight,tileSize:size,rect,tiles,refs:tiles.map(tile=>tile.reference)};
 }
 fail('BUDGET');
}
