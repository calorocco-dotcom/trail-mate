import {parseKml} from './kml.js';
import {trackSequenceSupported} from './kml-track-operations.js';
import {segmentOperationAvailability} from './kml-segment-operations.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',utf8=new TextEncoder();
const direct=(node,name)=>[...node.children].filter(child=>child.namespaceURI===NS&&child.localName===name);
function placemarks(xml){
 const result=[];
 function walk(node){if(node.namespaceURI!==NS)return;if(['Document','Folder'].includes(node.localName))[...node.children].forEach(walk);else if(node.localName==='Placemark')result.push(node);}
 [...xml.documentElement.children].forEach(walk);return result;
}
function field(node,name,text){
 const existing=direct(node,name);if(existing.length>1)throw Error(`KML ${name} 字段重复，不能明确修改`);
 const target=existing[0]??node.ownerDocument.createElementNS(NS,name);
 target.textContent=String(text);
 if(!existing.length){
  const order={name:0,visibility:1,description:2},rank=order[name]??3;
  const after=[...node.children].find(child=>(order[child.localName]??3)>rank);
  node.insertBefore(target,after??null);
 }
}
function feature(document,index){const nodes=placemarks(document.xml);if(!Number.isInteger(index)||index<0||index>=nodes.length)throw Error('请选择有效 KML 要素');return nodes[index];}
function finish(document){const source=utf8.encode(new XMLSerializer().serializeToString(document.xml));parseKml(source);return source;}
function text(value){if(typeof value!=='string'||value.includes('\0'))throw Error('KML 文字无效');return value;}
function position({longitude,latitude,altitude=null}){
 if(!Number.isFinite(longitude)||!Number.isFinite(latitude)||Math.abs(longitude)>180||Math.abs(latitude)>90||
  altitude!==null&&!Number.isFinite(altitude))throw Object.assign(Error('经纬度或高度无效'),{code:'KML_COORDINATE_RANGE'});
 return altitude===null?`${longitude},${latitude}`:`${longitude},${latitude},${altitude}`;
}
function container(document){
 const root=document.xml.documentElement,existing=[...root.children].find(n=>n.namespaceURI===NS&&['Document','Folder'].includes(n.localName));
 if(existing)return existing;
 const node=document.xml.createElementNS(NS,'Document');while(root.firstChild)node.append(root.firstChild);root.append(node);return node;
}
export function editKmlMetadata(source,{name,description}={}){
 const document=parseKml(source),node=container(document);
 if(name!==undefined)field(node,'name',text(name));if(description!==undefined)field(node,'description',text(description));
 return finish(document);
}
export function editKmlFeature(source,index,{name,description,visible}={}){
 const document=parseKml(source),node=feature(document,index);
 if(name!==undefined)field(node,'name',text(name));if(description!==undefined)field(node,'description',text(description));
 if(visible!==undefined){if(typeof visible!=='boolean')throw Error('显隐状态无效');field(node,'visibility',visible?'1':'0');}
 return finish(document);
}
export function removeKmlFeature(source,index){const document=parseKml(source);feature(document,index).remove();return finish(document);}
export function kmlGeometryEditors(source,index){
 const document=parseKml(source),result=[],segmentParents=new WeakMap();
 const geometry=n=>n.namespaceURI===NS&&['Point','LineString','LinearRing','Polygon','MultiGeometry','outerBoundaryIs','innerBoundaryIs'].includes(n.localName)||n.namespaceURI===GX&&['Track','MultiTrack'].includes(n.localName);
 function walk(node,path){
  if(node.namespaceURI===NS&&['Point','LineString','LinearRing'].includes(node.localName))result.push({path,type:node.localName,coordinates:direct(node,'coordinates')[0]?.textContent.trim().replace(/\s+/g,'\n')??'',splitSafe:![...node.getElementsByTagName('*')].some(child=>child.hasAttribute('id')),segmentOperations:segmentOperationAvailability(node,segmentParents)});
  else if(node.namespaceURI===GX&&node.localName==='Track')result.push({path,type:'gx:Track',coordinates:[...node.children].filter(n=>n.namespaceURI===GX&&n.localName==='coord').map(n=>n.textContent.trim().replace(/\s+/g,',')).join('\n'),sequenceSafe:trackSequenceSupported(node),splitSafe:![...node.getElementsByTagName('*')].some(child=>child.hasAttribute('id'))});
  else [...node.children].filter(geometry).forEach((child,i)=>walk(child,[...path,i]));
 }
 [...feature(document,index).children].filter(geometry).forEach((node,i)=>walk(node,[i]));return result;
}
export function addKmlWaypoint(source,{name='',description='',longitude,latitude,altitude=null,id}={}){
 const coordinate=position({longitude,latitude,altitude}),document=parseKml(source);
 id??='trailmate-wp-'+Array.from(crypto.getRandomValues(new Uint8Array(8)),b=>b.toString(16).padStart(2,'0')).join('');
 if(typeof id!=='string'||! /^[A-Za-z_][A-Za-z0-9._-]{0,63}$/.test(id)||[...document.xml.getElementsByTagName('*')].some(node=>node.getAttribute('id')===id))throw Error('航点 ID 无效或重复');
 const node=document.xml.createElementNS(NS,'Placemark');node.setAttribute('id',id);field(node,'name',text(name));field(node,'description',text(description));
 const point=document.xml.createElementNS(NS,'Point'),coordinates=document.xml.createElementNS(NS,'coordinates');coordinates.textContent=coordinate;
 if(altitude!==null)field(point,'altitudeMode','absolute');point.append(coordinates);node.append(point);container(document).append(node);return finish(document);
}
// Geometry paths refer to direct geometry children, then nested children.
// A gx:Track edit keeps when/angles/extensions and must retain point count.
export function editKmlCoordinates(source,index,path,points){
 if(!Array.isArray(path)||!path.length||path.some(n=>!Number.isInteger(n)||n<0)||!Array.isArray(points))throw Error('几何路径或坐标无效');
 const document=parseKml(source);let node=feature(document,index);
 const geometry=n=>n.namespaceURI===NS&&['Point','LineString','LinearRing','Polygon','MultiGeometry','outerBoundaryIs','innerBoundaryIs'].includes(n.localName)||n.namespaceURI===GX&&['Track','MultiTrack'].includes(n.localName);
 for(const step of path){node=[...node.children].filter(geometry)[step];if(!node)throw Error('几何路径不存在');}
 const encoded=points.map(position);
 if(node.namespaceURI===GX&&node.localName==='Track'){
  const coords=[...node.children].filter(n=>n.namespaceURI===GX&&n.localName==='coord');
  if(coords.length!==points.length)throw Error('gx:Track 修改须保留点数，不能丢失时间配对');
  if(points.some(point=>point.altitude===null||point.altitude===undefined))throw Error('gx:Track 坐标需要高度');
  coords.forEach((coord,i)=>{coord.textContent=encoded[i].replaceAll(',',' ');});
 }else if(node.namespaceURI===NS&&['Point','LineString','LinearRing'].includes(node.localName)){
  if(node.localName==='Point'&&points.length!==1)throw Error('航点需要一个坐标');
  field(node,'coordinates',encoded.join(' '));
 }else throw Error('请选择可编辑的单个点、线段、环或 gx:Track');
 return finish(document);
}
