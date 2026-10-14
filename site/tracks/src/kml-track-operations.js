import {parseKml} from './kml.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2';
const direct=(node,namespace,name)=>[...node.children].filter(child=>child.namespaceURI===namespace&&child.localName===name);
export function trackSequenceSupported(node){
 const count=direct(node,GX,'coord').length,angles=direct(node,GX,'angles').length;
 if(count<2||direct(node,NS,'when').length!==count||angles&&angles!==count)return false;
 for(const child of node.children){
  if(child.namespaceURI===GX&&['coord','angles','altitudeMode'].includes(child.localName))continue;
  if(child.namespaceURI!==NS||!['when','altitudeMode','extrude','tessellate','Model','ExtendedData'].includes(child.localName))return false;
 }
 for(const extended of direct(node,NS,'ExtendedData'))for(const entry of extended.children){
  if(entry.namespaceURI!==NS)return false;
  if(entry.localName==='Data'){if([...entry.children].some(child=>child.namespaceURI!==NS||!['displayName','value'].includes(child.localName)))return false;}
  else if(entry.localName==='SchemaData'){
   for(const field of entry.children){
    if(field.namespaceURI===NS&&field.localName==='SimpleData')continue;
    if(field.namespaceURI!==GX||field.localName!=='SimpleArrayData'||direct(field,GX,'value').length!==count||[...field.children].some(child=>child.namespaceURI!==GX||child.localName!=='value'))return false;
   }
  }else return false;
 }
 return true;
}
export function transformKmlTrack(source,index,path,{action,start,end}={}){
 const model=parseKml(source),features=[];
 const walk=node=>{if(node.namespaceURI===NS&&['Document','Folder'].includes(node.localName))[...node.children].forEach(walk);else if(node.namespaceURI===NS&&node.localName==='Placemark')features.push(node);};
 [...model.xml.documentElement.children].forEach(walk);
 if(!Number.isInteger(index)||!features[index]||!Array.isArray(path)||!path.length||path.some(value=>!Number.isInteger(value)||value<0))throw Error('Invalid geometry selection');
 const geometry=node=>node.namespaceURI===NS&&['Point','LineString','LinearRing','Polygon','MultiGeometry','outerBoundaryIs','innerBoundaryIs'].includes(node.localName)||node.namespaceURI===GX&&['Track','MultiTrack'].includes(node.localName);
 let node=features[index];for(const step of path){node=[...node.children].filter(geometry)[step];if(!node)throw Error('Geometry path missing');}
 if(node.namespaceURI!==GX||node.localName!=='Track'||!trackSequenceSupported(node))throw Error('Track sequence cannot be paired safely');
 const count=direct(node,GX,'coord').length;
 const retain=(track,first,last)=>{
  const arrays=direct(track,NS,'ExtendedData').flatMap(extended=>direct(extended,NS,'SchemaData').flatMap(schema=>direct(schema,GX,'SimpleArrayData')));
  const lists=[direct(track,NS,'when'),direct(track,GX,'coord'),direct(track,GX,'angles'),...arrays.map(array=>direct(array,GX,'value'))];
  lists.forEach(list=>list.forEach((element,index)=>{if(index<first||index>last)element.remove();}));
 };
 if(action==='crop'){
  if(!Number.isInteger(start)||!Number.isInteger(end)||start<0||end>=count||start>=end)throw Error('Crop requires at least two paired points');retain(node,start,end);
 }else if(action==='split'){
  if(!Number.isInteger(start)||start<1||start>=count-1)throw Error('Split requires two valid tracks');
  if([...node.getElementsByTagName('*')].some(child=>child.hasAttribute('id')))throw Error('Child IDs prevent an unambiguous split');
  const group=model.xml.createElementNS(GX,'gx:MultiTrack'),first=node.cloneNode(true),second=node.cloneNode(true);
  if(node.hasAttribute('id'))group.setAttribute('id',node.getAttribute('id'));first.removeAttribute('id');second.removeAttribute('id');
  // The container's altitude semantics match its original Track.
  for(const child of node.children)if(child.localName==='altitudeMode'&&[NS,GX].includes(child.namespaceURI))group.append(child.cloneNode(true));
  const interpolate=model.xml.createElementNS(GX,'gx:interpolate');interpolate.textContent='0';group.append(interpolate);
  retain(first,0,start);retain(second,start,count-1);group.append(first,second);node.replaceWith(group);
 }else throw Error('Timed Track reversal requires a separate derived route');
 const result=new TextEncoder().encode(new XMLSerializer().serializeToString(model.xml));parseKml(result);return result;
}
