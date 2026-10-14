import {kmlStyleText} from '../../i18n/kml-style-labels.js';
import {readKmlIconCrop,resolveKmlIconCrop} from './kml-icon-crop.js';
import {kmlCropText} from '../../i18n/kml-crop-labels.js';
const NS='http://www.opengis.net/kml/2.2';
const children=(node,name)=>[...(node?.children??[])].filter(child=>child.namespaceURI===NS&&child.localName===name);
const text=(node,name)=>children(node,name)[0]?.textContent.trim();
function featureNodes(xml,featureType='Placemark'){const result=[];function walk(node){if(node.namespaceURI!==NS)return;if(['Document','Folder'].includes(node.localName))[...node.children].forEach(walk);else if(node.localName===featureType)result.push(node);}[...xml.documentElement.children].forEach(walk);return result;}
const merge=(base,extra)=>{const result={...base};for(const name of ['line','poly','icon','label','balloon'])if(extra[name])result[name]={...base[name],...extra[name]};return result;};
function seedFor(value){let seed=2166136261;for(const char of value){seed^=char.charCodeAt(0);seed=Math.imul(seed,16777619);}return seed>>>0;}
// KML color is AABBGGRR. Random mode varies RGB within the supplied maxima;
// a stable preview seed keeps colors from changing on every React render.
export function kmlColor(raw='ffffffff',mode='normal',seed=1){
 if(!/^[0-9a-f]{8}$/i.test(raw))throw Error('KML 颜色需要 8 位 AABBGGRR');
 const channels=[raw.slice(6,8),raw.slice(4,6),raw.slice(2,4)].map(value=>parseInt(value,16));
 if(mode==='random')for(let i=0;i<3;i++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;channels[i]=Math.round(channels[i]*(seed/4294967296));}
 else if(mode!=='normal')throw Error('KML colorMode 无效');
 return {color:'#'+channels.map(value=>value.toString(16).padStart(2,'0')).join(''),opacity:parseInt(raw.slice(0,2),16)/255};
}
export function kmlStyleFields(node){
 const result={};
 for(const [name,kind] of [['LineStyle','line'],['PolyStyle','poly'],['IconStyle','icon'],['LabelStyle','label']]){
  const style=children(node,name)[0];if(!style)continue;const fields={};
  for(const field of ['color','colorMode','width','fill','outline','scale','heading']){const value=text(style,field);if(value!==undefined)fields[field]=value;}
  const hotSpot=children(style,'hotSpot')[0];if(hotSpot)fields.hotSpot=Object.fromEntries(['x','y','xunits','yunits'].map(key=>[key,hotSpot.getAttribute(key)??undefined]));
  const icon=children(style,'Icon')[0];if(kind==='icon'&&icon){fields.href=text(icon,'href')??'';try{fields.crop=readKmlIconCrop(icon);fields.cropInvalid=false;}catch{fields.crop=null;fields.cropInvalid=true;}}
  result[kind]=fields;
 }
 const balloon=children(node,'BalloonStyle')[0];if(balloon){const fields={};for(const name of ['bgColor','textColor','displayMode']){const value=text(balloon,name);if(value!==undefined)fields[name]=value;}const template=children(balloon,'text')[0];if(template)fields.text=template.textContent;const legacy=text(balloon,'color');if(fields.bgColor===undefined&&legacy!==undefined)fields.bgColor=legacy;result.balloon=fields;}
 return result;
}
export function createKmlStyleResolver(document,{iconResources,locale='zh-Hans',featureType='Placemark'}={}){
 if(!['Placemark','PhotoOverlay'].includes(featureType))throw Error('Unsupported style feature');
 const copy=kmlStyleText(locale);
 const selectors=new Map(),nodes=featureNodes(document.xml,featureType);
 for(const name of ['Style','StyleMap'])for(const node of document.xml.getElementsByTagNameNS(NS,name)){
  const id=node.getAttribute('id');if(!id)continue;if(selectors.has(id))selectors.set(id,null);else selectors.set(id,node);
 }
 function resolve(node,state,seen,issues){
  if(!node)return {};
  if(seen.has(node)||seen.size>=16){issues.push(copy.cycle);return {};}
  const next=new Set(seen);next.add(node);
  if(node.localName==='StyleMap'){
   const pairs=children(node,'Pair'),pair=pairs.find(pair=>text(pair,'key')===state)??pairs.find(pair=>text(pair,'key')==='normal');
   if(!pair){issues.push(copy.mapMissing);return {};}
   return merge(reference(text(pair,'styleUrl'),state,next,issues),resolve(children(pair,'Style')[0]??children(pair,'StyleMap')[0],state,next,issues));
  }
  if(node.localName!=='Style')return {};
  return kmlStyleFields(node);
 }
 function reference(url,state,seen,issues){
  if(!url)return {};if(!url.startsWith('#')){issues.push(copy.external);return {};}
  let id;try{id=decodeURIComponent(url.slice(1));}catch{issues.push(copy.refInvalid);return {};}
  if(!selectors.has(id)){issues.push(copy.missing);return {};}if(!selectors.get(id)){issues.push(copy.duplicate);return {};}
  return resolve(selectors.get(id),state,seen,issues);
 }
 return (index,{highlight=false}={})=>{
  const node=nodes[index];if(!node)throw Error('KML 样式要素不存在');const issues=[],state=highlight?'highlight':'normal';
  let style=reference(text(node,'styleUrl'),state,new Set(),issues);
  for(const inline of [...node.children].filter(n=>n.namespaceURI===NS&&['Style','StyleMap'].includes(n.localName)))style=merge(style,resolve(inline,state,new Set(),issues));
  const seed=seedFor(node.getAttribute('id')??String(index));
  const color=(fields,fallback)=>{if(!fields)return fallback;try{return kmlColor(fields.color,fields.colorMode,seed);}catch{issues.push(copy.colorInvalid);return fallback;}};
  const number=(raw,fallback,min,max)=>{if(raw===undefined)return fallback;if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(raw)||!Number.isFinite(Number(raw))||Number(raw)<min){issues.push(copy.valueInvalid);return fallback;}if(Number(raw)>max)issues.push(copy.valueBudget);return Math.min(max,Number(raw));};
  const boolean=(raw,fallback)=>{if(raw===undefined)return fallback;if(raw==='0'||raw==='false')return false;if(raw==='1'||raw==='true')return true;issues.push(copy.boolInvalid);return fallback;};
  const lineColor=color(style.line,{color:'#0c8d81',opacity:1}),polyColor=color(style.poly,{color:'#0c8d81',opacity:.12}),iconColor=color(style.icon,{color:'#0c8d81',opacity:.8}),labelColor=color(style.label,{color:'#405862',opacity:1});
  const weight=number(style.line?.width,style.line?1:3,0,32),scale=number(style.icon?.scale,1,0,5),radius=7*scale;
  let icon,image=iconResources?.get(style.icon?.href),crop;if(style.icon?.cropInvalid){issues.push(kmlCropText(locale));image=null;}else if(image){try{crop=resolveKmlIconCrop(style.icon?.crop,image);}catch{issues.push(kmlCropText(locale));image=null;}}
  if(image&&scale>0){
   const ratio=Math.min(scale,128/Math.max(crop.width,crop.height)),size=[crop.width*ratio,crop.height*ratio];
   const hotSpot=style.icon.hotSpot??{},axis=(name,length)=>{const raw=hotSpot[name]??'.5',unit=hotSpot[name+'units']??'fraction',value=Number(raw);if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(raw)||!Number.isFinite(value)||!['fraction','pixels','insetPixels'].includes(unit)){issues.push(copy.anchorInvalid);return length/2;}return unit==='fraction'?value*length:unit==='pixels'?value:length-value;};
   const anchor=[axis('x',crop.width)*ratio,(crop.height-axis('y',crop.height))*ratio];
   if(anchor.some(value=>!Number.isFinite(value)||Math.abs(value)>4096)){issues.push(copy.anchorBudget);anchor[0]=size[0]/2;anchor[1]=size[1]/2;}
   const heading=number(style.icon.heading,0,0,360);
   icon={url:image.url,size,anchor,heading,opacity:iconColor.opacity,color:iconColor.color,crop,imageSize:[image.width,image.height]};
   if(ratio<scale)issues.push(copy.sizeBudget);
  }else if(style.icon?.href)issues.push(copy.iconMissing);
  return {line:{...lineColor,weight},polygon:{...lineColor,weight,fillColor:polyColor.color,fillOpacity:polyColor.opacity,fill:boolean(style.poly?.fill,true),stroke:boolean(style.poly?.outline,true)},
   point:{color:iconColor.color,opacity:iconColor.opacity,fillColor:iconColor.color,fillOpacity:iconColor.opacity,radius},
   icon,iconHref:style.icon?.href,label:{...labelColor,scale:number(style.label?.scale,1,0,3)},issues:[...new Set(issues)],sourceStyle:style};
 };
}
