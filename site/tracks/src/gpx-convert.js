// GPX conversion runs exclusively in the browser, preserving disconnected segments.
const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
const NS='http://www.topografix.com/GPX/1/1';
const escape=value=>String(value).replace(/[&<>"']/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[char]));
export function convertGpx(input,{title='设备轨迹'}={}){
 if(!(input instanceof Uint8Array)||!input.length||input.length>8*1024*1024)throw Error('GPX 文件超过容量限制');
 if(typeof title!=='string'||!title.trim()||encoder.encode(title).length>128)throw Error('轨迹名称需要 1–128 字节');
 const source=input.slice(),text=decoder.decode(source);
 if(/<!\s*(DOCTYPE|ENTITY)\b/i.test(text))throw Error('不支持 GPX DTD 或实体定义');
 const document=new DOMParser().parseFromString(text,'application/xml'),root=document.documentElement;
 const legacy=!root.namespaceURI&&['Trail Mate','Trail Mate Linux'].includes(root.getAttribute('creator'));
 if(document.getElementsByTagName('parsererror').length||root.localName!=='gpx'||(!legacy&&root.namespaceURI!==NS)||root.getAttribute('version')!=='1.1')throw Error('需要有效的 GPX 1.1 文件');
 if(document.getElementsByTagName('*').length>200000)throw Error('GPX 元素数量超过限制');
 const stack=[[root,0]];while(stack.length){const [node,depth]=stack.pop();if(depth>64)throw Error('GPX 层级过深');for(const child of node.children)stack.push([child,depth+1]);}
 const namespace=legacy?null:NS;
 const children=(node,name)=>[...node.children].filter(child=>child.namespaceURI===namespace&&child.localName===name);
 const value=(node,name)=>{const nodes=children(node,name);if(nodes.length>1)throw Error('GPX 单值字段重复');return nodes[0]?.textContent??'';};
 const scalar=raw=>{if(typeof raw!=='string'||!raw.trim()||! /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(raw.trim()))throw Error('GPX 数值格式无效');const number=Number(raw);if(!Number.isFinite(number))throw Error('GPX 数值无效');return number;};
 const serializer=new XMLSerializer();let pointCount=0,featureCount=0;const issues=legacy?['此 Trail Mate 旧记录缺少 GPX 命名空间，已按设备记录格式读取。']:[];
 const point=node=>{
  if(++pointCount>100000)throw Error('GPX 坐标点数量超过限制');
  const lat=scalar(node.getAttribute('lat')),lon=scalar(node.getAttribute('lon'));
  if(Math.abs(lat)>90||lon< -180||lon>=180)throw Error('GPX 经纬度超出范围');
  const elevation=value(node,'ele'),rawTime=value(node,'time');let time=null;
  if(rawTime){if(!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/.test(rawTime)||!Number.isFinite(Date.parse(rawTime)))throw Error('GPX 时间需要有效的时区');const date=new Date(rawTime.slice(0,10)+'T00:00:00Z');if(date.toISOString().slice(0,10)!==rawTime.slice(0,10))throw Error('GPX 日期无效');time=new Date(Date.parse(rawTime)).toISOString();}
  return {lat,lon,alt:elevation?scalar(elevation):null,time};
 };
 const data=(name,content)=>`<Data name="${name}"><value>${escape(content)}</value></Data>`;
 const geometry=points=>{
  if(points.length>1&&points.every(p=>p.alt!==null&&p.time!==null))return `<gx:Track><altitudeMode>absolute</altitudeMode>${points.map(p=>`<when>${p.time}</when>`).join('')}${points.map(p=>`<gx:coord>${p.lon} ${p.lat} ${p.alt}</gx:coord>`).join('')}</gx:Track>`;
  const type=points.length===1?'Point':'LineString';
  return `<${type}>${points.every(p=>p.alt!==null)?'<altitudeMode>absolute</altitudeMode>':''}<coordinates>${points.map(p=>`${p.lon},${p.lat}${p.alt===null?'':','+p.alt}`).join(' ')}</coordinates></${type}>`;
 };
 const placemark=(node,groups,fallback)=>{
  const nonempty=groups.filter(group=>group.length);if(!nonempty.length)return '';
  if(++featureCount>5000)throw Error('GPX 要素数量超过限制');
  const geometries=nonempty.map(geometry),shape=geometries.length===1?geometries[0]:`<MultiGeometry>${geometries.join('')}</MultiGeometry>`;
  // The original GPX feature remains inert text, retaining per-point extensions and metadata.
  return `<Placemark><name>${escape(value(node,'name')||fallback)}</name><description>${escape(value(node,'desc'))}</description><ExtendedData>${data('trailmate.gpx.source',serializer.serializeToString(node))}${data('trailmate.gpx.segment_times',JSON.stringify(nonempty.map(group=>group.map(p=>p.time))))}</ExtendedData>${shape}</Placemark>`;
 };
 const features=[];
 for(const node of children(root,'wpt'))features.push(placemark(node,[[point(node)]],'路标'));
 for(const node of children(root,'rte'))features.push(placemark(node,[children(node,'rtept').map(point)],'路线'));
 for(const node of children(root,'trk'))features.push(placemark(node,children(node,'trkseg').map(segment=>children(segment,'trkpt').map(point)),'轨迹'));
 if(!pointCount)throw Error('GPX 没有坐标点');
 issues.push('GPX 已转换为 KML；源要素与扩展保存在 ExtendedData，原文件请单独保留。');
 const metadata=children(root,'metadata');if(metadata.length>1)throw Error('GPX metadata 重复');
 const name=metadata.length?value(metadata[0],'name')||title:title;
 const xml=`<?xml version="1.0" encoding="UTF-8"?><kml xmlns="http://www.opengis.net/kml/2.2" xmlns:gx="http://www.google.com/kml/ext/2.2"><Document><name>${escape(name)}</name><ExtendedData>${data('trailmate.gpx.metadata',metadata.map(node=>serializer.serializeToString(node)).join(''))}${data('trailmate.gpx.root_extensions',children(root,'extensions').map(node=>serializer.serializeToString(node)).join(''))}${data('trailmate.recording.warnings',issues.join('\n'))}</ExtendedData>${features.join('')}</Document></kml>`;
 const kml=encoder.encode(xml);if(kml.length>8*1024*1024)throw Error('转换后的 KML 超过容量限制');
 return {source,kml,profile:'gpx-1.1',pointCount,issues};
}
