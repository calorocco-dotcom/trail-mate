import {parseKml} from './kml.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2';
export function transformKmlLine(source,index,path,{action,start,end}={}){
 const model=parseKml(source),features=[];
 const walk=node=>{if(node.namespaceURI===NS&&['Document','Folder'].includes(node.localName))[...node.children].forEach(walk);else if(node.namespaceURI===NS&&node.localName==='Placemark')features.push(node);};
 [...model.xml.documentElement.children].forEach(walk);
 if(!Number.isInteger(index)||!features[index]||!Array.isArray(path)||!path.length||path.some(value=>!Number.isInteger(value)||value<0))throw Error('Invalid geometry selection');
 const geometry=node=>node.namespaceURI===NS&&['Point','LineString','LinearRing','Polygon','MultiGeometry','outerBoundaryIs','innerBoundaryIs'].includes(node.localName)||node.namespaceURI===GX&&['Track','MultiTrack'].includes(node.localName);
 let node=features[index];for(const step of path){node=[...node.children].filter(geometry)[step];if(!node)throw Error('Geometry path missing');}
 if(node.namespaceURI!==NS||node.localName!=='LineString')throw Error('This operation requires an untimed LineString');
 const coordinates=[...node.children].filter(child=>child.namespaceURI===NS&&child.localName==='coordinates');
 if(coordinates.length!==1)throw Error('Ambiguous coordinate field');const points=coordinates[0].textContent.trim().split(/\s+/);
 if(action==='reverse')coordinates[0].textContent=points.reverse().join(' ');
 else if(action==='crop'){
  if(!Number.isInteger(start)||!Number.isInteger(end)||start<0||end>=points.length||start>=end)throw Error('Crop requires at least two points');
  coordinates[0].textContent=points.slice(start,end+1).join(' ');
 }else if(action==='split'){
  if(!Number.isInteger(start)||start<1||start>=points.length-1)throw Error('Split requires two valid line segments');
  // Clone static line properties, but never duplicate IDs in unknown children.
  if([...node.getElementsByTagName('*')].some(child=>child.hasAttribute('id')))throw Error('Child IDs prevent an unambiguous split');
  const group=model.xml.createElementNS(NS,'MultiGeometry'),first=node.cloneNode(true),second=node.cloneNode(true);
  if(node.hasAttribute('id'))group.setAttribute('id',node.getAttribute('id'));
  first.removeAttribute('id');second.removeAttribute('id');
  [...first.children].find(child=>child.namespaceURI===NS&&child.localName==='coordinates').textContent=points.slice(0,start+1).join(' ');
  [...second.children].find(child=>child.namespaceURI===NS&&child.localName==='coordinates').textContent=points.slice(start).join(' ');
  group.append(first,second);node.replaceWith(group);
 }else throw Error('Unknown line operation');
 const result=new TextEncoder().encode(new XMLSerializer().serializeToString(model.xml));parseKml(result);return result;
}
