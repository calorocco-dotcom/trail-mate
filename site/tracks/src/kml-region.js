const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',MAX_LAT=85.0511287798066;
const children=(node,name,ns=NS)=>[...(node?.children??[])].filter(child=>child.namespaceURI===ns&&child.localName===name);
function scalar(node,name,fallback,ns=NS){const found=children(node,name,ns);if(found.length>1||found[0]?.children.length)throw Error('Ambiguous region scalar');return found[0]?.textContent.trim()??fallback;}
export function kmlRegionNumber(raw){if(typeof raw!=='string'||raw.length>64||! /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(raw.trim()))throw Error('Invalid region number');const value=Number(raw);if(!Number.isFinite(value))throw Error('Invalid region number');return value;}
export function validateKmlRegion(value){
 if(!value||!['north','south','east','west','minAltitude','maxAltitude','minLodPixels','maxLodPixels','minFadeExtent','maxFadeExtent'].every(key=>Number.isFinite(value[key])))throw Error('Invalid region');
 if(value.north>90||value.south< -90||value.north<value.south||Math.abs(value.east)>180||Math.abs(value.west)>180||value.minAltitude>value.maxAltitude||value.minLodPixels<0||value.maxLodPixels!==-1&&value.maxLodPixels<=value.minLodPixels||value.minFadeExtent<0||value.maxFadeExtent<0||!['clampToGround','relativeToGround','absolute','clampToSeaFloor','relativeToSeaFloor'].includes(value.altitudeMode))throw Error('Invalid region bounds or LOD');return value;
}
export function readKmlRegion(node){
 const regions=children(node,'Region');if(!regions.length)return null;
 try{if(regions.length!==1)throw Error('Ambiguous region');const region=regions[0],boxes=children(region,'LatLonAltBox'),lods=children(region,'Lod');if(boxes.length!==1||lods.length!==1)throw Error('Missing region box or LOD');
  const box=boxes[0],lod=lods[0],modes=[...children(box,'altitudeMode'),...children(box,'altitudeMode',GX)];if(modes.length>1||modes[0]?.children.length)throw Error('Ambiguous altitude mode');
  const value={altitudeMode:modes[0]?.textContent.trim()??'clampToGround'};for(const key of ['north','south','east','west','minAltitude','maxAltitude'])value[key]=kmlRegionNumber(scalar(box,key,['minAltitude','maxAltitude'].includes(key)?'0':undefined));for(const key of ['minLodPixels','maxLodPixels','minFadeExtent','maxFadeExtent'])value[key]=kmlRegionNumber(scalar(lod,key,key==='minLodPixels'?undefined:key==='maxLodPixels'?'-1':'0'));
  validateKmlRegion(value);return {...value,id:region.getAttribute('id'),altitudeLimited:value.altitudeMode!=='clampToGround',projectionLimited:Math.abs(value.north)>MAX_LAT||Math.abs(value.south)>MAX_LAT,fadeOverlap:value.maxLodPixels!==-1&&value.minFadeExtent+value.maxFadeExtent>value.maxLodPixels-value.minLodPixels};
 }catch{return {invalid:true};}
}
export function collectKmlRegionScopes(model,overlays={overlays:[]}){
 const features=[],overlayRegions=new Map(),profileOwners=[],declared=[];
 const walk=(node,inherited=null)=>{if(node.namespaceURI!==NS||!['Document','Folder','Placemark'].includes(node.localName))return;const own=readKmlRegion(node),region=own??inherited;if(own)declared.push(own);if(node.localName==='Placemark')features.push(region);else for(const child of node.children)walk(child,region);};
 if(model?.xml)for(const node of model.xml.documentElement.children)walk(node);else for(const feature of model?.features??[])features.push(null);
 const all=Object.fromEntries(['GroundOverlay','ScreenOverlay','PhotoOverlay'].map(type=>[type,model?.xml?[...model.xml.getElementsByTagNameNS(NS,type)]:[]]));for(const overlay of overlays.overlays){let region=null;const node=all[overlay.type??'GroundOverlay']?.[overlay.domIndex??overlay.index],own=readKmlRegion(node);if(own)declared.push(own);for(let ancestor=node;ancestor&&ancestor!==model.xml.documentElement;ancestor=ancestor.parentElement){if(ancestor.namespaceURI===NS&&['GroundOverlay','ScreenOverlay','PhotoOverlay','Document','Folder'].includes(ancestor.localName)){const local=readKmlRegion(ancestor);if(local){region=local;break;}}}overlayRegions.set(overlay.index,region);}
 const geometry=(value,index)=>{if(value.geometries)value.geometries.forEach(child=>geometry(child,index));else if(['LineString','Track'].includes(value.type)&&value.points?.length>1)profileOwners.push(index);};for(const [index,feature] of (model?.features??[]).entries())for(const value of feature.geometries??[])geometry(value,index);
 return {features,overlays:overlayRegions,profileOwners,declared,invalid:declared.filter(region=>region.invalid).length,altitude:declared.filter(region=>region.altitudeLimited).length,projection:declared.filter(region=>region.projectionLimited).length,overlap:declared.filter(region=>region.fadeOverlap).length};
}
export function kmlRegionOpacity(region,viewport){
 if(!region)return 1;if(region.invalid)return 0;if(!viewport)return 1;
 const {bounds,zoom}=viewport;if(!bounds||![bounds.west,bounds.east,bounds.north,bounds.south,zoom].every(Number.isFinite))return 0;
 let width=region.east-region.west;if(width<0)width+=360;const center=(bounds.west+bounds.east)/2,west=region.west+360*Math.round((center-region.west-width/2)/360),east=west+width;
 if(region.north<bounds.south||region.south>bounds.north||bounds.east-bounds.west<360&&(east<bounds.west||west>bounds.east))return 0;
 const mercator=latitude=>{const radians=Math.min(MAX_LAT,Math.max(-MAX_LAT,latitude))*Math.PI/180;return Math.log(Math.tan(Math.PI/4+radians/2));},world=256*2**zoom,pixels=Math.sqrt(width/360*world*Math.abs(mercator(region.north)-mercator(region.south))/(2*Math.PI)*world),max=region.maxLodPixels===-1?Infinity:region.maxLodPixels;
 if(pixels<region.minLodPixels||pixels>=max)return 0;if(region.minFadeExtent>0&&pixels<region.minLodPixels+region.minFadeExtent)return Math.min(1,(pixels-region.minLodPixels)/region.minFadeExtent);if(region.maxFadeExtent>0&&pixels>=max-region.maxFadeExtent)return Math.min(1,(max-pixels)/region.maxFadeExtent);return 1;
}
export function kmlRegionAppearance(style,opacity){return {...style,line:{...style.line,opacity:(style.line.opacity??1)*opacity},polygon:{...style.polygon,opacity:(style.polygon.opacity??1)*opacity,fillOpacity:(style.polygon.fillOpacity??1)*opacity},point:style.point,label:{...style.label,opacity:(style.label.opacity??1)*opacity}};}
export function kmlRegionGeometryVisible(value,opacity){return Boolean(value&&opacity>0&&(value.geometries?value.geometries.some(child=>kmlRegionGeometryVisible(child,opacity)):value.type==='Point'?opacity>.5:value.rings||value.points?.length));}
export function kmlRegionProfileOpacity(point,regions){const owner=Number.isInteger(point?.feature)?point.feature:Number.isInteger(point?.segment)?regions.scopes.profileOwners[point.segment]:null;return owner==null?1:regions.opacities[owner]??0;}
