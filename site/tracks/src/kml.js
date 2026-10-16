import {parseKmlTimeValue} from './kml-time.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2';
const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
export const KML_LIMIT=Object.freeze({bytes:8*1024*1024,points:100000,features:5000,elements:200000,depth:64});
const children=(node,name,namespace=NS)=>[...node.children].filter(child=>child.namespaceURI===namespace&&child.localName===name);
const value=(node,name,fallback='',namespace=NS)=>children(node,name,namespace)[0]?.textContent??fallback;
function number(raw){if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(raw.trim()))throw Error('KML 坐标格式错误');const n=Number(raw);if(!Number.isFinite(n))throw Error('KML 坐标不是有限数值');return n;}
function coordinate(raw,gx=false){
 const parts=raw.trim().split(gx?/\s+/:',');if(parts.length<2||parts.length>3||parts.some(part=>!part.trim()))throw Error('KML 坐标需要经度、纬度和可选高度');
 const [longitude,latitude,altitude=null]=parts.map(number);if(Math.abs(longitude)>180||Math.abs(latitude)>90)throw Error('KML 经纬度超出范围');return {longitude,latitude,altitude};
}
function altitudeMode(node){return value(node,'altitudeMode',value(node,'altitudeMode','clampToGround',GX));}
function isGeometry(node){return (node.namespaceURI===NS&&['Point','LineString','LinearRing','Polygon','MultiGeometry'].includes(node.localName))||(node.namespaceURI===GX&&['Track','MultiTrack'].includes(node.localName));}

export function parseKml(input,{limits=KML_LIMIT}={}){
 const raw=typeof input==='string'?encoder.encode(input):input;
 if(!(raw instanceof Uint8Array)||!raw.length||raw.length>limits.bytes)throw Error('KML 文件超过容量限制');
 const text=decoder.decode(raw);if(/<!\s*(DOCTYPE|ENTITY)\b/i.test(text))throw Error('不支持带 DTD 或实体定义的 KML');
 const xml=new DOMParser().parseFromString(text,'application/xml');if(xml.getElementsByTagName('parsererror').length)throw Error('KML XML 解析失败');
 const root=xml.documentElement;if(root.localName!=='kml'||root.namespaceURI!==NS)throw Error('需要 KML 2.2 命名空间');
 if(xml.getElementsByTagName('*').length>limits.elements)throw Error('KML 元素数量超过限制');
 let pointCount=0;const features=[],issues=[],styles=new Map();
 const addPoints=n=>{pointCount+=n;if(pointCount>limits.points)throw Error('KML 坐标点数量超过限制');};
 function geometry(node,depth=0){
   if(depth>limits.depth)throw Error('KML 几何层级过深');const type=node.localName,mode=altitudeMode(node);
   if(type==='MultiGeometry'||type==='MultiTrack')return {type,id:node.getAttribute('id'),geometries:[...node.children].filter(isGeometry).map(n=>geometry(n,depth+1)),interpolate:value(node,'interpolate','0',GX)==='1'};
   if(type==='Polygon'){
     const rings=['outerBoundaryIs','innerBoundaryIs'].flatMap(kind=>children(node,kind).flatMap(boundary=>children(boundary,'LinearRing').map(ring=>({kind,...geometry(ring,depth+1)}))));
     if(!rings.some(r=>r.kind==='outerBoundaryIs'))throw Error('KML Polygon 缺少外边界');return {type,altitudeMode:mode,rings};
   }
   let points,time=[],rawTime=[];
   if(type==='Track'){
     points=children(node,'coord',GX).map(n=>coordinate(n.textContent,true));
     rawTime=children(node,'when').map(n=>n.textContent);
     time=rawTime.map(text=>{if(!text.trim())return null;try{return parseKmlTimeValue(text,{full:true}).start;}catch{throw Error('KML 轨迹时间无效');}});
     if(time.length&&time.length!==points.length)throw Error('KML 轨迹时间与坐标数量不一致');
   }else points=value(node,'coordinates').trim().split(/\s+/).filter(Boolean).map(raw=>coordinate(raw));
   if(type==='Point'&&points.length!==1)throw Error('KML Point 必须只有一个坐标');
   if(['LineString','LinearRing'].includes(type)&&points.length<2)throw Error('KML 线段坐标不足');
   if(type==='LinearRing'&&(points.length<4||points[0].longitude!==points.at(-1).longitude||points[0].latitude!==points.at(-1).latitude))throw Error('KML LinearRing 必须闭合');
   addPoints(points.length);return {type,id:node.getAttribute('id'),points,time,rawTime,altitudeMode:mode,extrude:value(node,'extrude','0')==='1',tessellate:value(node,'tessellate','0')==='1'};
 }
 function feature(node,path,depth){
   if(depth>limits.depth)throw Error('KML 内容层级过深');
   if(node.namespaceURI!==NS)return;
   const type=node.localName,name=value(node,'name'),id=node.getAttribute('id');
   if(type==='Document'||type==='Folder')return {type,id,name,visible:!['0','false'].includes(value(node,'visibility','1').trim()),children:[...node.children].map(child=>feature(child,[...path,name],depth+1)).filter(Boolean)};
   if(type==='Style'||type==='StyleMap'){if(id){if(styles.has(id))issues.push({type:'duplicate-style-id',id});styles.set(id,{type,xml:new XMLSerializer().serializeToString(node)});}return;}
   if(type!=='Placemark'){
     if(['NetworkLink','PhotoOverlay'].includes(type))issues.push({type:'preserved-unrendered',element:type,name});
     return;
   }
   if(features.length>=limits.features)throw Error('KML 地理要素数量超过限制');
   const record={type,id,name,path,visible:!['0','false'].includes(value(node,'visibility','1').trim()),description:value(node,'description'),styleUrl:value(node,'styleUrl'),geometries:[...node.children].filter(isGeometry).map(n=>geometry(n)),extendedData:[],timestamp:value(children(node,'TimeStamp')[0]??node,'when'),timespan:null};
   const span=children(node,'TimeSpan')[0];if(span)record.timespan={begin:value(span,'begin'),end:value(span,'end')};
   for(const extended of children(node,'ExtendedData'))for(const data of children(extended,'Data'))record.extendedData.push({name:data.getAttribute('name'),displayName:value(data,'displayName'),value:value(data,'value')});
   for(const extended of children(node,'ExtendedData'))for(const schema of children(extended,'SchemaData'))for(const data of children(schema,'SimpleData'))record.extendedData.push({schemaUrl:schema.getAttribute('schemaUrl'),name:data.getAttribute('name'),value:data.textContent});
   if(!record.geometries.length)issues.push({type:'no-supported-geometry',id,name});features.push(record);return record;
 }
 const tree=[...root.children].map(n=>feature(n,[],0)).filter(Boolean);
 for(const node of xml.getElementsByTagNameNS(GX,'Tour'))issues.push({type:'preserved-unrendered',element:'gx:Tour',name:value(node,'name')});
 for(const node of xml.getElementsByTagNameNS(NS,'Model'))issues.push({type:'preserved-unrendered',element:'Model',name:value(node,'name')});
 const resources=[];
 for(const node of xml.getElementsByTagName('*')){
   const supported=node.namespaceURI===NS&&['NetworkLink','GroundOverlay','ScreenOverlay','PhotoOverlay','Model'].includes(node.localName)||node.namespaceURI===GX&&node.localName==='Tour';
   if(!supported)continue;
   if(resources.length>=limits.features)throw Error('KML 资源数量超过限制');
   const path=[];let depth=0;
   for(let parent=node.parentElement;parent&&parent!==root;parent=parent.parentElement){
     if(++depth>limits.depth)throw Error('KML 资源层级过深');
     if(parent.namespaceURI===NS&&['Document','Folder','Placemark'].includes(parent.localName)){const name=value(parent,'name');if(name)path.unshift(name);}
   }
   const link=children(node,'Link')[0]??children(node,'Url')[0]??children(node,'Icon')[0];
   resources.push({type:node.namespaceURI===GX?'gx:'+node.localName:node.localName,id:node.getAttribute('id'),name:value(node,'name'),path,href:link?value(link,'href'):''});
 }
 return {source:raw.slice(),xml,tree,features,styles,issues,resources,pointCount};
}

// Safe presentation only. The original description remains in source/XML.
// External images become inert references for the separately verified asset UI.
export function descriptionContent(description){
 const parsed=document.createElement('template'),out=document.createElement('div'),images=[];
 parsed.innerHTML=description;
 const allowed=new Set(['P','BR','STRONG','B','EM','I','UL','OL','LI','BLOCKQUOTE','TABLE','TBODY','THEAD','TR','TD','TH','SPAN','A']);
 function append(source,target){for(const node of source.childNodes){if(node.nodeType===3){target.append(document.createTextNode(node.textContent));continue;}if(node.nodeType!==1)continue;
   if(['SCRIPT','STYLE','IFRAME','OBJECT','EMBED'].includes(node.tagName))continue;
   if(node.tagName==='IMG'){images.push({src:node.getAttribute('src')??'',alt:node.getAttribute('alt')??''});continue;}
   if(!allowed.has(node.tagName)){append(node,target);continue;}
   const element=document.createElement(node.tagName.toLowerCase());
   if(node.tagName==='A'){const href=node.getAttribute('href')??'';try{const url=new URL(href);if(['http:','https:'].includes(url.protocol)){element.href=url.href;element.rel='noopener noreferrer';element.target='_blank';}}catch{}}
   append(node,element);target.append(element);
 }}
 append(parsed.content,out);return {html:out.innerHTML,text:out.textContent,images};
}
