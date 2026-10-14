import {parseKml,KML_LIMIT} from './kml.js';
import {groundOverlayBox,groundOverlayQuad} from './kml-ground-overlays.js';
import {validateKmlIconCrop} from './kml-icon-crop.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',utf8=new TextEncoder();
const children=(node,name,ns=NS)=>[...(node?.children??[])].filter(n=>n.namespaceURI===ns&&n.localName===name);
const raw=(node,name,fallback='',ns=NS)=>children(node,name,ns)[0]?.textContent??fallback;
const fail=key=>{throw Object.assign(Error(key),{code:'KML_OVERLAY_'+key.toUpperCase()});};
function unique(node,name,ns=NS){const found=children(node,name,ns);if(found.length>1)fail('ambiguous');return found[0];}
const order=['name','visibility','open','author','link','address','AddressDetails','phoneNumber','Snippet','description','Camera','LookAt','TimeStamp','TimeSpan','styleUrl','Style','StyleMap','Region','ExtendedData','color','drawOrder','Icon','altitude','altitudeMode','LatLonBox','LatLonQuad','north','south','east','west','rotation','href','refreshMode','refreshInterval','viewRefreshMode','viewRefreshTime','viewBoundScale','viewFormat','httpQuery','x','y','w','h','coordinates'];
function element(node,name,ns=NS){const found=unique(node,name,ns);if(found)return found;const child=node.ownerDocument.createElementNS(ns,ns===GX?(node.lookupPrefix(GX)||'gx')+':'+name:name),rank=order.indexOf(name);const after=[...node.children].find(n=>(n.namespaceURI===NS||n.namespaceURI===GX)&&order.indexOf(n.localName)>rank);node.insertBefore(child,after??null);return child;}
function field(node,name,value,ns=NS){element(node,name,ns).textContent=String(value);}
function number(value){const text=String(value).trim();if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(text)||!Number.isFinite(Number(text)))fail('number');return Number(text);}
function string(value){if(typeof value!=='string'||/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(value))fail('text');return value;}
function overlays(model){const result=[];function walk(node){if(node.namespaceURI!==NS)return;if(node.localName==='GroundOverlay')result.push(node);else if(['kml','Document','Folder'].includes(node.localName))[...node.children].forEach(walk);}walk(model.xml.documentElement);return result;}
function target(model,index){const nodes=overlays(model);if(!Number.isInteger(index)||index<0||index>=nodes.length)fail('selection');return nodes[index];}
function finish(model){const source=utf8.encode(new XMLSerializer().serializeToString(model.xml));parseKml(source);return source;}
// DOM ordinal is deliberately independent of visibility, validity and draw order.
export function groundOverlayEditors(model){
 return overlays(model).map((node,index)=>{
  const icon=children(node,'Icon')[0],box=children(node,'LatLonBox')[0],quad=children(node,'LatLonQuad',GX)[0],points=raw(quad,'coordinates').trim().split(/\s+/).filter(Boolean).map(p=>p.split(','));
  const path=[];let inheritedHidden=false;for(let p=node.parentElement;p&&p!==model.xml.documentElement;p=p.parentElement){if(p.namespaceURI===NS&&['Document','Folder'].includes(p.localName)){const name=raw(p,'name');if(name)path.unshift(name);inheritedHidden||=['0','false'].includes(raw(p,'visibility','1').trim());}}
  return {index,id:node.getAttribute('id'),path,inheritedHidden,name:raw(node,'name'),description:raw(node,'description'),visible:!['0','false'].includes(raw(node,'visibility','1').trim()),color:raw(node,'color','ffffffff'),drawOrder:raw(node,'drawOrder','0'),href:raw(icon,'href'),mode:quad&&!box?'quad':'box',box:Object.fromEntries(['north','south','east','west','rotation'].map(key=>[key,raw(box,key,key==='rotation'?'0':'')])),points:Array.from({length:4},(_,i)=>({longitude:points[i]?.[0]??'',latitude:points[i]?.[1]??'',altitude:points[i]?.[2]??null})),cropEnabled:['x','y','w','h'].some(key=>children(icon,key,GX).length>0),crop:Object.fromEntries(['x','y','w','h'].map(key=>[key,raw(icon,key,key==='x'||key==='y'?'0':'-1',GX)]))};
 });
}
function patch(node,changes){
 const allowed=['name','description','visible','color','drawOrder','href','footprint','crop'];if(Object.keys(changes).some(key=>!allowed.includes(key)))fail('selection');
 for(const key of ['name','description'])if(key in changes)field(node,key,string(changes[key]));
 if('visible' in changes){if(typeof changes.visible!=='boolean')fail('selection');field(node,'visibility',changes.visible?'1':'0');}
 if('color' in changes){if(!/^[\da-f]{8}$/i.test(changes.color))fail('color');field(node,'color',changes.color.toLowerCase());}
 if('drawOrder' in changes){const value=String(changes.drawOrder).trim();if(!/^[+-]?\d+$/.test(value)||Number(value)<-2147483648||Number(value)>2147483647)fail('order');field(node,'drawOrder',value);}
 if('href' in changes)field(element(node,'Icon'),'href',string(changes.href));
 if('crop' in changes){const icon=element(node,'Icon');if(changes.crop===null){for(const key of ['x','y','w','h'])children(icon,key,GX).forEach(n=>n.remove());}else{const crop=Object.fromEntries(['x','y','w','h'].map(key=>{const value=String(changes.crop[key]).trim();if(!/^[+-]?\d+$/.test(value))fail('crop');return [key,Number(value)];}));try{validateKmlIconCrop(crop);}catch{fail('crop');}for(const key of ['x','y','w','h'])field(icon,key,crop[key],GX);}}
 if('footprint' in changes){
  const box=unique(node,'LatLonBox'),quad=unique(node,'LatLonQuad',GX),footprint=changes.footprint;if(box&&quad)fail('ambiguous');
  if(footprint?.mode==='box'){
   const values=Object.fromEntries(['north','south','east','west','rotation'].map(key=>[key,number(footprint.box?.[key]??(key==='rotation'?0:''))]));try{groundOverlayBox(values);}catch{fail('box');}
   // Removing an ID-bearing footprint could break an external KML Update.
   if(quad&&[quad,...quad.getElementsByTagName('*')].some(n=>n.hasAttribute('id')))fail('referenced');
   quad?.remove();const existing=box??element(node,'LatLonBox');for(const [key,value] of Object.entries(values))if(!box||raw(box,key,key==='rotation'?'0':'').trim()!==String(footprint.box[key]).trim())field(existing,key,value);
  }else if(footprint?.mode==='quad'){
   if(!Array.isArray(footprint.points)||footprint.points.length!==4)fail('quad');const encoded=footprint.points.map(p=>{const longitude=number(p.longitude),latitude=number(p.latitude);return longitude+','+latitude+(p.altitude===null||p.altitude===undefined?'':','+number(p.altitude));}).join(' ');try{groundOverlayQuad(encoded);}catch{fail('quad');}
   if(box&&[box,...box.getElementsByTagName('*')].some(n=>n.hasAttribute('id')))fail('referenced');
   box?.remove();field(quad??element(node,'LatLonQuad',GX),'coordinates',encoded);
  }else fail('selection');
 }
}
export function editGroundOverlay(source,index,changes){const model=parseKml(source);patch(target(model,index),changes);return Object.keys(changes).length?finish(model):model.source;}
export function addGroundOverlay(source,changes){
 const model=parseKml(source);if(groundOverlayEditors(model).length>=KML_LIMIT.features)fail('limit');if(!changes.footprint)fail('box');const root=model.xml.documentElement;
 let container=children(root,'Document')[0];if(!container){container=model.xml.createElementNS(NS,'Document');while(root.firstChild)container.append(root.firstChild);root.append(container);}
 const node=model.xml.createElementNS(NS,'GroundOverlay');let id;do{id='trailmate-overlay-'+crypto.randomUUID();}while([...model.xml.getElementsByTagName('*')].some(n=>n.getAttribute('id')===id));node.setAttribute('id',id);patch(node,changes);container.append(node);return finish(model);
}
export function removeGroundOverlay(source,index){const model=parseKml(source),node=target(model,index),ids=new Set([node,...node.getElementsByTagName('*')].map(n=>n.getAttribute('id')).filter(Boolean));if([...model.xml.getElementsByTagName('*')].some(n=>!node.contains(n)&&ids.has(n.getAttribute('targetId'))))fail('referenced');node.remove();return finish(model);}
