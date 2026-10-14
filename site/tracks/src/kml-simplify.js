import {parseKml} from './kml.js';
import {simplifyKmlPoints} from './kml-simplify-points.js';
import {trackSequenceSupported} from './kml-track-operations.js';
import {summarizeKml} from './kml-summary.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',XMLNS='http://www.w3.org/2000/xmlns/';
export function simplifyKmlGeometry(source,index,path,tolerance){
 const model=parseKml(source),features=[];
 const fail=code=>{throw Object.assign(Error(code),{code});};
 const walk=node=>{if(node.namespaceURI===NS&&['Document','Folder'].includes(node.localName))[...node.children].forEach(walk);else if(node.namespaceURI===NS&&node.localName==='Placemark')features.push(node);};[...model.xml.documentElement.children].forEach(walk);
 const geometry=node=>node.namespaceURI===NS&&['Point','LineString','LinearRing','Polygon','MultiGeometry','outerBoundaryIs','innerBoundaryIs'].includes(node.localName)||node.namespaceURI===GX&&['Track','MultiTrack'].includes(node.localName);
 if(!Number.isInteger(index)||!features[index]||!Array.isArray(path)||!path.length||path.some(value=>!Number.isInteger(value)||value<0))fail('SIMPLIFY_INPUT');
 let node=features[index];for(const step of path){node=[...node.children].filter(geometry)[step];if(!node)fail('SIMPLIFY_INPUT');}
 const timed=node.namespaceURI===GX&&node.localName==='Track',line=node.namespaceURI===NS&&node.localName==='LineString';if(!line&&!timed)fail('SIMPLIFY_UNSUPPORTED');
 if([...node.attributes].some(attribute=>attribute.namespaceURI!==XMLNS&&!['id','targetId'].includes(attribute.name)))fail('SIMPLIFY_UNSUPPORTED');
 const direct=(parent,namespace,name)=>[...parent.children].filter(child=>child.namespaceURI===namespace&&child.localName===name);
 if(timed&&!trackSequenceSupported(node))fail('SIMPLIFY_UNSUPPORTED');
 if(line){
  if(direct(node,NS,'coordinates').length!==1)fail('SIMPLIFY_UNSUPPORTED');
  for(const child of node.children)if(!(child.namespaceURI===NS&&['coordinates','altitudeMode','extrude','tessellate'].includes(child.localName)||child.namespaceURI===GX&&['altitudeMode','altitudeOffset','drawOrder'].includes(child.localName))||child.children.length||[...child.attributes].some(attribute=>attribute.namespaceURI!==XMLNS&&attribute.name!=='id'))fail('SIMPLIFY_UNSUPPORTED');
 }
 let value=model.features[index];for(const step of path){const children=value.geometries??value.rings;value=children?.[step];if(!value)fail('SIMPLIFY_INPUT');}
 const arrays=timed?direct(node,NS,'ExtendedData').flatMap(extended=>direct(extended,NS,'SchemaData').flatMap(schema=>direct(schema,GX,'SimpleArrayData'))):[];
 const lists=timed?[direct(node,NS,'when'),direct(node,GX,'coord'),direct(node,GX,'angles'),...arrays.map(array=>direct(array,GX,'value'))]:[];
 const anchors=[];lists.forEach(list=>list.forEach((element,i)=>{if(element.hasAttribute('id')||[...element.getElementsByTagName('*')].some(child=>child.hasAttribute('id')))anchors.push(i);}));
 const {indices}=simplifyKmlPoints(value.points,tolerance,{anchors,time:value.time??[]}),points=indices.map(i=>value.points[i]);
 const beforeStatistics=summarizeKml(model);
 if(timed){const kept=new Set(indices);lists.forEach(list=>list.forEach((element,i)=>{if(!kept.has(i))element.remove();}));}
 else{const coordinates=direct(node,NS,'coordinates')[0],tokens=coordinates.textContent.trim().split(/\s+/);coordinates.textContent=indices.map(i=>tokens[i]).join(' ');}
 const result=new TextEncoder().encode(new XMLSerializer().serializeToString(model.xml)),afterModel=parseKml(result);return {source:result,geometryPath:path.slice(),points,before:value.points.length,after:indices.length,statistics:{before:beforeStatistics,after:summarizeKml(afterModel)}};
}
