import {kmlColor} from './kml-style.js';
import {KML_LIMIT} from './kml.js';
import {readKmlIconCrop} from './kml-icon-crop.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2';
const children=(node,name,ns=NS)=>[...(node?.children??[])].filter(child=>child.namespaceURI===ns&&child.localName===name);
const text=(node,name,fallback='',ns=NS)=>children(node,name,ns)[0]?.textContent.trim()??fallback;
const numeric=raw=>{if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(raw))throw Error('Invalid overlay number');const n=Number(raw);if(!Number.isFinite(n))throw Error('Invalid overlay number');return n;};
export function groundOverlayQuad(raw){
 const points=raw.trim().split(/\s+/).map(tuple=>{const parts=tuple.split(',');if(parts.length<2||parts.length>3)throw Error('Invalid overlay tuple');const values=parts.map(numeric);if(Math.abs(values[0])>180||Math.abs(values[1])>90)throw Error('Invalid overlay coordinate');return {longitude:values[0],latitude:values[1]};});
 if(points.length!==4)throw Error('Four overlay corners required');
 for(let i=1;i<4;i++)points[i].longitude+=360*Math.round((points[0].longitude-points[i].longitude)/360);
 for(let i=0;i<4;i++){const a=points[i],b=points[(i+1)%4],c=points[(i+2)%4],cross=(b.longitude-a.longitude)*(c.latitude-b.latitude)-(b.latitude-a.latitude)*(c.longitude-b.longitude);if(!(cross>0))throw Error('Overlay must be convex and counterclockwise');}
 return [points[3],points[2],points[1],points[0]];
}
export function groundOverlayBox({north,south,east,west,rotation=0}){
 if(![north,south,east,west,rotation].every(Number.isFinite)||north>90||south< -90||north<=south||Math.abs(east)>540||Math.abs(west)>540||Math.abs(rotation)>180)throw Error('Invalid overlay box');
 let width=east-west;if(width<0)width+=360;if(!(width>0&&width<=360))throw Error('Invalid overlay width');
 const x=west+width/2,y=(north+south)/2,angle=rotation*Math.PI/180,c=Math.cos(angle),s=Math.sin(angle);
 return [[west,north],[west+width,north],[west+width,south],[west,south]].map(([lng,lat])=>({longitude:x+c*(lng-x)-s*(lat-y),latitude:y+s*(lng-x)+c*(lat-y)}));
}
export function collectGroundOverlays(model,{limit=KML_LIMIT.features}={}){
 const overlays=[],issues=[];if(!model?.xml)return {overlays,issues};
 const nodes=[...model.xml.getElementsByTagNameNS(NS,'GroundOverlay')];if(nodes.length>limit)issues.push({code:'limit',name:''});
 for(const [index,node] of nodes.slice(0,limit).entries()){
  const name=text(node,'name');try{
   let visible=true,scoped=false;for(let parent=node;parent&&parent!==model.xml.documentElement;parent=parent.parentElement){if(parent.namespaceURI===NS&&['GroundOverlay','Document','Folder'].includes(parent.localName)){const flag=text(parent,'visibility','1');if(!['0','1','true','false'].includes(flag))throw Error('Invalid visibility');visible&&=!['0','false'].includes(flag);scoped||=Boolean(children(parent,'Region').length);}}
   const box=children(node,'LatLonBox')[0],quad=children(node,'LatLonQuad',GX)[0];if(Boolean(box)===Boolean(quad))throw Error('One overlay footprint required');
   const corners=quad?groundOverlayQuad(text(quad,'coordinates')):groundOverlayBox(Object.fromEntries(['north','south','east','west','rotation'].map(key=>[key,numeric(text(box,key,key==='rotation'?'0':''))])));
   if(corners.some(point=>Math.abs(point.latitude)>85.0511287798066)){issues.push({code:'projection',name});continue;}
   const order=text(node,'drawOrder','0');if(!/^[+-]?\d+$/.test(order)||Number(order)<-2147483648||Number(order)>2147483647)throw Error('Invalid draw order');
   const icon=children(node,'Icon')[0],href=text(icon,'href');let crop;try{crop=readKmlIconCrop(icon);}catch{issues.push({code:'crop',name});continue;}
   const mode=text(node,'altitudeMode',text(node,'altitudeMode','clampToGround',GX));if(mode!=='clampToGround')issues.push({code:'altitude',name});
   if(scoped)issues.push({code:'scope',name});
   overlays.push({id:node.getAttribute('id'),index,name,visible,href,corners,crop,drawOrder:Number(order),...kmlColor(text(node,'color','ffffffff'))});
  }catch{issues.push({code:'invalid',name});}
 }
 overlays.sort((a,b)=>a.drawOrder-b.drawOrder||a.index-b.index);return {overlays,issues};
}
// Map an image's unit square to projected top-left/top-right/bottom-right/
// bottom-left corners. Solving relative to the first corner limits cancellation.
export function overlayHomography(corners){
 if(corners.length!==4||corners.some(p=>!Number.isFinite(p.x)||!Number.isFinite(p.y)))return null;
 const origin=corners[0],points=corners.map(p=>({x:p.x-origin.x,y:p.y-origin.y})),uv=[[0,0],[1,0],[1,1],[0,1]],rows=[];
 for(let i=0;i<4;i++){const [u,v]=uv[i],{x,y}=points[i];rows.push([u,v,1,0,0,0,-u*x,-v*x,x],[0,0,0,u,v,1,-u*y,-v*y,y]);}
 for(let column=0;column<8;column++){let pivot=column;for(let row=column+1;row<8;row++)if(Math.abs(rows[row][column])>Math.abs(rows[pivot][column]))pivot=row;if(Math.abs(rows[pivot][column])<1e-12)return null;[rows[column],rows[pivot]]=[rows[pivot],rows[column]];const divisor=rows[column][column];for(let j=column;j<9;j++)rows[column][j]/=divisor;for(let row=0;row<8;row++)if(row!==column){const multiple=rows[row][column];for(let j=column;j<9;j++)rows[row][j]-=multiple*rows[column][j];}}
 const h=rows.map(row=>row[8]);if(!h.every(Number.isFinite)||uv.some(([u,v])=>h[6]*u+h[7]*v+1<=1e-12))return null;
 return {origin,h};
}
