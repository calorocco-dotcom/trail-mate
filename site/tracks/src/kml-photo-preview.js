import {kmlColor} from './kml-style.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2';
const children=(node,name,ns=NS)=>[...(node?.children??[])].filter(child=>child.namespaceURI===ns&&child.localName===name);
const one=(node,name,ns=NS)=>{const rows=children(node,name,ns);if(rows.length>1)throw Error('Ambiguous photo');return rows[0];};
const raw=(node,name,fallback='')=>one(node,name)?.textContent??fallback;
const numeric=value=>{const s=String(value).trim();if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(s)||!Number.isFinite(Number(s)))throw Error('Invalid photo number');return Number(s);};
export function collectPhotoOverlays(model){
 const overlays=[],issues=[];if(!model?.xml)return {overlays,issues};const ordinal=new Map([...model.xml.getElementsByTagNameNS(NS,'PhotoOverlay')].map((node,index)=>[node,index]));
 function walk(node){if(node.namespaceURI!==NS)return;if(['kml','Document','Folder'].includes(node.localName)){for(const child of node.children)walk(child);return;}if(node.localName!=='PhotoOverlay')return;
  const row={type:'PhotoOverlay',index:overlays.length,domIndex:ordinal.get(node),id:node.getAttribute('id'),name:children(node,'name')[0]?.textContent??'',description:children(node,'description')[0]?.textContent??'',visible:true,point:null};overlays.push(row);
  try{
   one(node,'name');one(node,'description');
   for(let parent=node;parent&&parent!==model.xml.documentElement;parent=parent.parentElement)if(parent.namespaceURI===NS&&['PhotoOverlay','Document','Folder'].includes(parent.localName)){const value=raw(parent,'visibility','1').trim();if(!['0','1','true','false'].includes(value))throw Error('Invalid visibility');row.visible&&=!['0','false'].includes(value);}
   const point=one(node,'Point');if(point){const coordinates=raw(point,'coordinates').trim().split(',');if(coordinates.length<2||coordinates.length>3)throw Error('Invalid point');const [longitude,latitude,altitude=null]=coordinates.map(numeric);if(Math.abs(longitude)>180||Math.abs(latitude)>90)throw Error('Invalid point');row.point={longitude,latitude,altitude};}
   row.href=raw(one(node,'Icon'),'href');row.shape=raw(node,'shape','rectangle').trim();row.rotation=numeric(raw(node,'rotation','0'));row.drawOrder=numeric(raw(node,'drawOrder','0'));Object.assign(row,kmlColor(raw(node,'color','ffffffff').trim()));
   if(!['rectangle','cylinder','sphere'].includes(row.shape)||Math.abs(row.rotation)>180||!Number.isInteger(row.drawOrder)||row.drawOrder< -2147483648||row.drawOrder>2147483647)throw Error('Invalid shape');
   const volume=one(node,'ViewVolume');row.volume=Object.fromEntries(['leftFov','rightFov','bottomFov','topFov','near'].map(key=>[key,numeric(raw(volume,key,'0'))]));const v=row.volume;
   if(v.leftFov< -180||v.rightFov>180||v.leftFov>=v.rightFov||v.bottomFov< -90||v.topFov>90||v.bottomFov>=v.topFov||v.near<=0||row.shape==='rectangle'&&(v.leftFov<=-90||v.rightFov>=90)||row.shape!=='sphere'&&(v.bottomFov<=-90||v.topFov>=90))throw Error('Invalid projection');
   if(one(node,'Camera')&&one(node,'LookAt'))throw Error('Ambiguous viewpoint');const camera=one(node,'Camera')??one(node,'LookAt');row.viewpoint=camera?{type:camera.localName,heading:numeric(raw(camera,'heading','0')),tilt:numeric(raw(camera,'tilt','0')),roll:numeric(raw(camera,'roll','0'))}:null;
   const pyramid=one(node,'ImagePyramid');if(pyramid){row.pyramid={tileSize:numeric(raw(pyramid,'tileSize','256')),maxWidth:numeric(raw(pyramid,'maxWidth','0')),maxHeight:numeric(raw(pyramid,'maxHeight','0')),gridOrigin:raw(pyramid,'gridOrigin','lowerLeft').trim()};const p=row.pyramid;if(!['lowerLeft','upperLeft'].includes(p.gridOrigin)||[p.tileSize,p.maxWidth,p.maxHeight].some(value=>!Number.isInteger(value)||value<1||value>2147483647)||!Number.isInteger(Math.log2(p.tileSize)))throw Error('Invalid pyramid');}
  }catch{row.invalid=true;issues.push({code:'invalid',name:row.name});}
 }
 walk(model.xml.documentElement);return {overlays,issues};
}
// Ray components are measured in the KML photo's original viewpoint axes.
// Rectangle intersects its plane; cylinder uses radius/linear height; sphere
// uses longitude/latitude. UV is bottom-left based, matching the GL texture.
export function photoProjectionUv(photo,[x,y,z]){
 if(![x,y,z].every(Number.isFinite)||Math.hypot(x,y,z)===0||photo.invalid)return null;const v=photo.volume,r=Math.PI/180;let u,w;
 if(photo.shape==='rectangle'){if(z<=0)return null;u=(x/z-Math.tan(v.leftFov*r))/(Math.tan(v.rightFov*r)-Math.tan(v.leftFov*r));w=(y/z-Math.tan(v.bottomFov*r))/(Math.tan(v.topFov*r)-Math.tan(v.bottomFov*r));}
 else{const radius=Math.hypot(x,z);u=(Math.atan2(x,z)/r-v.leftFov)/(v.rightFov-v.leftFov);w=photo.shape==='sphere'?(Math.atan2(y,radius)/r-v.bottomFov)/(v.topFov-v.bottomFov):(y/radius-Math.tan(v.bottomFov*r))/(Math.tan(v.topFov*r)-Math.tan(v.bottomFov*r));}
 if(![u,w].every(Number.isFinite)||u<0||u>1||w<0||w>1)return null;const angle=photo.rotation*r,c=Math.cos(angle),s=Math.sin(angle),a=u-.5,b=w-.5;u=.5+c*a+s*b;w=.5-s*a+c*b;return u<0||u>1||w<0||w>1?null:[u,w];
}
