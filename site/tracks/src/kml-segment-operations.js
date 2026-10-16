import {parseKml} from './kml.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',XMLNS='http://www.w3.org/2000/xmlns/';
export function segmentOperationAvailability(node,parents=new WeakMap()){
 const empty={inGroup:false,earlier:false,later:false,mergeNext:false,mergedPoints:0},parent=node.parentElement;
 if(node.namespaceURI!==NS||node.localName!=='LineString'||parent?.namespaceURI!==NS||parent.localName!=='MultiGeometry')return empty;
 let cache=parents.get(parent);if(!cache){const siblings=[...parent.children].filter(child=>child.namespaceURI===NS&&child.localName==='LineString');cache={siblings,positions:new Map(siblings.map((child,index)=>[child,index])),info:new WeakMap()};parents.set(parent,cache);}
 const position=cache.positions.get(node),next=cache.siblings[position+1],result={...empty,inGroup:true,earlier:position>0,later:position<cache.siblings.length-1};if(!next)return result;
 const info=element=>{
  if(cache.info.has(element))return cache.info.get(element);
  const properties=[],coordinates=[];let known=true;
  for(const attribute of element.attributes)if(attribute.name!=='id'&&attribute.namespaceURI!==XMLNS)known=false;
  for(const child of element.children){
   if(child.namespaceURI===NS&&child.localName==='coordinates'){if(child.children.length||[...child.attributes].some(attribute=>attribute.namespaceURI!==XMLNS))known=false;coordinates.push(child.textContent.trim().split(/\s+/));continue;}
   if(child.namespaceURI===NS&&['altitudeMode','extrude','tessellate'].includes(child.localName)||child.namespaceURI===GX&&['altitudeMode','altitudeOffset','drawOrder'].includes(child.localName)){
    if(child.children.length||[...child.attributes].some(attribute=>attribute.namespaceURI!==XMLNS)||properties.some(property=>property[0]===child.namespaceURI&&property[1]===child.localName))known=false;
    properties.push([child.namespaceURI,child.localName,child.textContent.trim()]);
   }else known=false;
  }
  if(coordinates.length!==1)known=false;const points=coordinates[0]??[];
  const value={known,properties:JSON.stringify(properties.sort((a,b)=>JSON.stringify(a).localeCompare(JSON.stringify(b)))),count:points.length,first:points[0]?.split(',').map(Number),last:points.at(-1)?.split(',').map(Number)};
  cache.info.set(element,value);return value;
 };
 const first=info(node),second=info(next),idLoss=next.hasAttribute('id')||[...next.getElementsByTagName('*')].some(child=>child.hasAttribute('id'));
 const connected=first.last&&second.first&&first.last.length===second.first.length&&first.last.every((value,index)=>value===second.first[index]||index===0&&Math.abs(value-second.first[index])===360);
 if(first.known&&second.known&&!idLoss&&connected&&first.properties===second.properties){result.mergeNext=true;result.mergedPoints=first.count+second.count-1;}
 return result;
}
export function transformKmlSegments(source,index,path,{action}={}){
 const model=parseKml(source),features=[];
 const walk=node=>{if(node.namespaceURI===NS&&['Document','Folder'].includes(node.localName))[...node.children].forEach(walk);else if(node.namespaceURI===NS&&node.localName==='Placemark')features.push(node);};
 [...model.xml.documentElement.children].forEach(walk);
 if(!Number.isInteger(index)||!features[index]||!Array.isArray(path)||!path.length||path.some(value=>!Number.isInteger(value)||value<0))throw Error('Invalid geometry selection');
 const geometry=node=>node.namespaceURI===NS&&['Point','LineString','LinearRing','Polygon','MultiGeometry','outerBoundaryIs','innerBoundaryIs'].includes(node.localName)||node.namespaceURI===GX&&['Track','MultiTrack'].includes(node.localName);
 let node=features[index];for(const step of path){node=[...node.children].filter(geometry)[step];if(!node)throw Error('Geometry path missing');}
 const available=segmentOperationAvailability(node);if(!available.inGroup)throw Error('Select a LineString within MultiGeometry');
 const parent=node.parentElement,siblings=[...parent.children].filter(child=>child.namespaceURI===NS&&child.localName==='LineString'),position=siblings.indexOf(node);
 if(action==='earlier'&&available.earlier)parent.insertBefore(node,siblings[position-1]);
 else if(action==='later'&&available.later)parent.insertBefore(node,siblings[position+1].nextSibling);
 else if(action==='merge'&&available.mergeNext){
  const next=siblings[position+1],coordinates=element=>[...element.children].find(child=>child.namespaceURI===NS&&child.localName==='coordinates');
  const target=coordinates(node),points=coordinates(next).textContent.trim().split(/\s+/);target.textContent=target.textContent.trim()+' '+points.slice(1).join(' ');next.remove();
 }else throw Error('Segment operation cannot preserve properties, endpoints and IDs');
 const geometryPath=[...path.slice(0,-1),[...parent.children].filter(geometry).indexOf(node)],result=new TextEncoder().encode(new XMLSerializer().serializeToString(model.xml));parseKml(result);return {source:result,geometryPath};
}
