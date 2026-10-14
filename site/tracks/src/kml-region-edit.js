import {parseKml} from './kml.js';import {readKmlRegion,kmlRegionNumber,validateKmlRegion} from './kml-region.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',utf8=new TextEncoder(),types=new Set(['Document','Folder','Placemark','GroundOverlay','NetworkLink','ScreenOverlay','PhotoOverlay']);
export const kmlRegionFields=['north','south','east','west','minAltitude','maxAltitude','minLodPixels','maxLodPixels','minFadeExtent','maxFadeExtent'];
const boxFields=kmlRegionFields.slice(0,6),lodFields=kmlRegionFields.slice(6),defaults={north:'',south:'',east:'',west:'',minAltitude:'0',maxAltitude:'0',altitudeMode:'clampToGround',minLodPixels:'0',maxLodPixels:'-1',minFadeExtent:'0',maxFadeExtent:'0'};
const children=(node,name,ns=NS)=>[...(node?.children??[])].filter(child=>child.namespaceURI===ns&&child.localName===name);
const fail=reason=>{throw Object.assign(Error(reason),{code:'KML_REGION_EDIT_'+reason.toUpperCase()});};
function own(node){
 const regions=children(node,'Region'),region=regions[0],boxes=children(region,'LatLonAltBox'),lods=children(region,'Lod'),box=boxes[0],lod=lods[0],modes=[...children(box,'altitudeMode'),...children(box,'altitudeMode',GX)],values={mode:region?'local':'inherit',...defaults};
 let ambiguous=regions.length>1||boxes.length>1||lods.length>1||modes.length>1||Boolean(modes[0]?.children.length);
 for(const key of kmlRegionFields){const found=children(boxFields.includes(key)?box:lod,key);ambiguous||=found.length>1||found.some(child=>child.children.length>0);if(found[0])values[key]=found[0].textContent;}
 if(modes[0])values.altitudeMode=modes[0].textContent.trim();return {region,box,lod,modeNode:modes[0],values,ambiguous:Boolean(ambiguous),needsRepair:Boolean(region&&readKmlRegion(node)?.invalid)};
}
function owners(model){
 const result=[];let featureIndex=0;
 const walk=(node,path,inherited)=>{
  if(!(node.namespaceURI===NS&&types.has(node.localName)||node.namespaceURI===GX&&node.localName==='Tour'))return;
  const name=children(node,'name')[0]?.textContent??'',id=node.getAttribute('id'),local=own(node),type=node.namespaceURI===GX?'gx:Tour':node.localName;
  result.push({index:result.length,type,name,id,path,featureIndex:type==='Placemark'?featureIndex++:null,local:local.values,inherited,node,...local});
  if(['Document','Folder'].includes(type)){const next=local.region?{owner:name||id||type,values:local.values,ambiguous:local.ambiguous,needsRepair:local.needsRepair}:inherited;for(const child of node.children)walk(child,[...path,name||id||type],next);}
 };
 for(const node of model.xml.documentElement.children)walk(node,[],null);return result;
}
export function kmlRegionEditors(model){return owners(model).map(({node,region,box,lod,modeNode,values,...record})=>record);}
function target(model,index){const row=owners(model)[index];if(!Number.isInteger(index)||index<0||!row)fail('selection');if(row.ambiguous)fail('ambiguous');return row;}
export function validateKmlRegionPatch(patch){
 if(!patch||typeof patch!=='object'||Array.isArray(patch)||Object.keys(patch).some(key=>!['mode','altitudeMode',...kmlRegionFields].includes(key))||!['inherit','local'].includes(patch.mode))fail('invalid');
 if(patch.mode==='inherit')return {mode:'inherit'};try{return {mode:'local',...validateKmlRegion(Object.fromEntries([['altitudeMode',patch.altitudeMode],...kmlRegionFields.map(key=>[key,kmlRegionNumber(patch[key])])]))};}catch{fail('invalid');}
}
export function activeKmlRegionValues(values){return values.mode==='inherit'?{mode:'inherit'}:{mode:values.mode,altitudeMode:values.altitudeMode,...Object.fromEntries(kmlRegionFields.map(key=>[key,values[key]]))};}
export function kmlRegionBoundsValues(bounds){if(!bounds||!['north','south','east','west'].every(key=>Number.isFinite(bounds[key])))return null;const longitude=value=>{const normalized=((value+180)%360+360)%360-180;return normalized===-180&&value>0?180:normalized;};return {north:String(bounds.north),south:String(bounds.south),east:String(longitude(bounds.east)),west:String(longitude(bounds.west))};}
function equivalent(left,right){try{const a=validateKmlRegionPatch(left),b=validateKmlRegionPatch(right);return a.mode===b.mode&&(a.mode==='inherit'||a.altitudeMode===b.altitudeMode&&kmlRegionFields.every(key=>a[key]===b[key]));}catch{return false;}}
function plan(model,row,patch){
 const normalized=validateKmlRegionPatch(patch),noop=equivalent(patch,row.local)&&(!row.region||!row.needsRepair),modeNS=['clampToSeaFloor','relativeToSeaFloor'].includes(normalized.altitudeMode)?GX:NS,removed=normalized.mode==='inherit'&&row.region?row.region:row.modeNode&&row.modeNode.namespaceURI!==modeNS?row.modeNode:null;
 const ids=removed?new Set([removed,...removed.getElementsByTagName('*')].map(node=>node.getAttribute('id')).filter(Boolean)):new Set(),referenced=Boolean(removed&&[...model.xml.getElementsByTagName('*')].some(node=>!removed.contains(node)&&ids.has(node.getAttribute('targetId'))));
 const loss=new Set();if(normalized.mode==='inherit'&&row.region)for(const node of [row.region,...row.region.getElementsByTagName('*')]){for(const attribute of node.attributes)loss.add(node.tagName+' · @'+attribute.name);if(node.namespaceURI!==NS||!['Region','LatLonAltBox','Lod','altitudeMode',...kmlRegionFields].includes(node.localName))loss.add(node.tagName);if([...node.childNodes].some(child=>child.nodeType===8))loss.add(node.tagName+' · comment');}
 return {noop,referenced,loss:[...loss].slice(0,32),lossCount:loss.size,normalized};
}
export function kmlRegionChangePlan(model,index,patch){return plan(model,target(model,index),patch);}
const order=['name','visibility','open','author','link','address','AddressDetails','phoneNumber','Snippet','description','Camera','LookAt','TimeStamp','TimeSpan','styleUrl','Style','StyleMap','Region','Metadata','ExtendedData','AbstractFeatureSimpleExtensionGroup','AbstractFeatureObjectExtensionGroup','Schema',...types,'color','drawOrder','Icon','Link','Url','refreshVisibility','flyToView','altitude','altitudeMode','LatLonBox','overlayXY','screenXY','rotationXY','size','rotation','ViewVolume','ImagePyramid','shape','Point','LineString','LinearRing','Polygon','MultiGeometry','Model','Track','MultiTrack','Tour','Playlist','LatLonQuad'];
function insert(parent,child,names){const rank=names.indexOf(child.localName),after=[...parent.children].find(node=>[NS,GX].includes(node.namespaceURI)&&names.indexOf(node.localName)>rank);parent.insertBefore(child,after??null);}
function create(parent,name,ns=NS){const prefix=parent.lookupPrefix(ns)??(ns===GX?'gx':parent.namespaceURI===ns?parent.prefix:null),node=parent.ownerDocument.createElementNS(ns,prefix?prefix+':'+name:name);return node;}
function setScalar(parent,name,value,{required=false,ns=NS,fallback,ordered}={}){
 let node=[...children(parent,name),...(name==='altitudeMode'?children(parent,name,GX):[])][0];const text=String(value);
 if(!node&&!required&&text===fallback)return;
 if(node){const same=name==='altitudeMode'?node.textContent.trim()===text:(()=>{try{return kmlRegionNumber(node.textContent)===Number(value);}catch{return false;}})();if(same&&node.namespaceURI===ns)return;
  if(node.namespaceURI!==ns){const replacement=create(parent,name,ns);for(const attribute of node.attributes)replacement.setAttributeNS(attribute.namespaceURI,attribute.name,attribute.value);for(const child of node.childNodes)replacement.append(child.cloneNode(true));node.replaceWith(replacement);node=replacement;}
 }else{node=create(parent,name,ns);insert(parent,node,ordered);}
 const content=[...node.childNodes].filter(child=>[3,4].includes(child.nodeType));if(content.length){content[0].nodeValue=text;for(const child of content.slice(1))child.nodeValue='';}else node.insertBefore(parent.ownerDocument.createTextNode(text),node.firstChild);
}
export function editKmlRegion(source,index,patch,{allowLoss=false}={}){
 const model=parseKml(source),row=target(model,index),changes=plan(model,row,patch),values=changes.normalized;if(changes.noop)return model.source;if(changes.referenced)fail('referenced');if(changes.loss.length&&allowLoss!==true)fail('loss');
 if(values.mode==='inherit')row.region?.remove();else{
  let region=row.region;if(!region){region=create(row.node,'Region');insert(row.node,region,order);}let box=row.box,lod=row.lod;if(!box){box=create(region,'LatLonAltBox');insert(region,box,['ObjectSimpleExtensionGroup','LatLonAltBox','Lod','RegionSimpleExtensionGroup','RegionObjectExtensionGroup']);}if(!lod){lod=create(region,'Lod');insert(region,lod,['ObjectSimpleExtensionGroup','LatLonAltBox','Lod','RegionSimpleExtensionGroup','RegionObjectExtensionGroup']);}
  for(const key of boxFields)setScalar(box,key,values[key],{required:['north','south','east','west'].includes(key),fallback:defaults[key],ordered:['ObjectSimpleExtensionGroup','north','south','east','west','AbstractLatLonBoxSimpleExtensionGroup','AbstractLatLonBoxObjectExtensionGroup','minAltitude','maxAltitude','altitudeMode','LatLonAltBoxSimpleExtensionGroup','LatLonAltBoxObjectExtensionGroup']});
  const modeNS=['clampToSeaFloor','relativeToSeaFloor'].includes(values.altitudeMode)?GX:NS;setScalar(box,'altitudeMode',values.altitudeMode,{ns:modeNS,fallback:defaults.altitudeMode,ordered:['ObjectSimpleExtensionGroup','north','south','east','west','AbstractLatLonBoxSimpleExtensionGroup','AbstractLatLonBoxObjectExtensionGroup','minAltitude','maxAltitude','altitudeMode','LatLonAltBoxSimpleExtensionGroup','LatLonAltBoxObjectExtensionGroup']});
  for(const key of lodFields)setScalar(lod,key,values[key],{required:key==='minLodPixels',fallback:defaults[key],ordered:['ObjectSimpleExtensionGroup',...lodFields,'LodSimpleExtensionGroup','LodObjectExtensionGroup']});
 }
 const result=utf8.encode(new XMLSerializer().serializeToString(model.xml)),verified=parseKml(result);if(values.mode==='local'&&readKmlRegion(target(verified,index).node)?.invalid)fail('invalid');return result;
}
