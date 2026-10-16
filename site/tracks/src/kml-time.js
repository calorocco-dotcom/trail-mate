import {validTrackDateTime} from './track-datetime.js';
const NS='http://www.opengis.net/kml/2.2';
const children=(node,name)=>[...(node?.children??[])].filter(child=>child.namespaceURI===NS&&child.localName===name);
const utc=(year,month,day,hour=0,minute=0,second=0,millisecond=0)=>{const date=new Date(0);date.setUTCFullYear(year,month-1,day);date.setUTCHours(hour,minute,second,millisecond);return date.getTime();};
// Preview policy: floating XML dates use UTC; preserve their original spelling.
// Date-only precision covers its calendar day/month/year; dateTime is an instant.
export function parseKmlTimeValue(raw,{full=false}={}){
 if(typeof raw!=='string'||raw.length>63)throw Error('KML_TIME_INVALID');
 const text=raw.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g,''),match=/^(-?\d{4,})(?:-(\d{2})(?:-(\d{2})(?:T(\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?)?)?)?(Z|[+-]\d{2}:\d{2})?$/.exec(text);
 if(!match||full&&match[4]===undefined)throw Error('KML_TIME_INVALID');
 const [,yearText,monthText,dayText,hourText,minuteText,secondText,fraction='',zone='']=match;
 const composed=`${yearText}-${monthText??'01'}-${dayText??'01'}T${hourText??'00'}:${minuteText??'00'}:${secondText??'00'}${fraction?'.'+fraction:''}${zone}`;
 if(!validTrackDateTime(composed))throw Error('KML_TIME_INVALID');
 const negative=yearText.startsWith('-'),absolute=BigInt(negative?yearText.slice(1):yearText),year=Number(negative?1n-absolute:absolute);
 if(!Number.isSafeInteger(year))throw Error('KML_TIME_UNSUPPORTED');
 const month=Number(monthText??1),day=Number(dayText??1),hour=Number(hourText??0),minute=Number(minuteText??0),second=Number(secondText??0),millisecond=Number(fraction.slice(0,3).padEnd(3,'0'));
 const offset=!zone||zone==='Z'?0:(zone[0]==='-'?-1:1)*(Number(zone.slice(1,3))*60+Number(zone.slice(4)))*60000;
 const start=utc(year,month,day,hour,minute,second,millisecond)-offset;
 let end=start;
 if(hourText===undefined){const next=dayText!==undefined?utc(year,month,day+1):monthText!==undefined?utc(year,month+1,1):utc(year+1,1,1);end=next-offset-1;}
 if(!Number.isFinite(start)||!Number.isFinite(end)||Math.abs(start)>8640000000000000||Math.abs(end)>8640000000000000)throw Error('KML_TIME_UNSUPPORTED');
 return {start,end,precision:hourText!==undefined?'instant':dayText!==undefined?'day':monthText!==undefined?'month':'year'};
}
export function formatKmlTimeInput(timestamp){
 if(!Number.isFinite(timestamp))return '';
 const date=new Date(timestamp),year=date.getUTCFullYear(),encodedYear=year>0?String(year).padStart(4,'0'):'-'+String(1-year).padStart(4,'0'),pad=value=>String(value).padStart(2,'0');
 return `${encodedYear}-${pad(date.getUTCMonth()+1)}-${pad(date.getUTCDate())}T${pad(date.getUTCHours())}:${pad(date.getUTCMinutes())}:${pad(date.getUTCSeconds())}.${String(date.getUTCMilliseconds()).padStart(3,'0')}Z`;
}
export function readKmlTimePrimitive(node){
 const primitives=[...children(node,'TimeStamp'),...children(node,'TimeSpan')];if(!primitives.length)return null;
 try{
  if(primitives.length!==1)throw Error('KML_TIME_INVALID');
  const primitive=primitives[0];
  if(primitive.localName==='TimeStamp'){const when=children(primitive,'when');if(when.length!==1)throw Error('KML_TIME_INVALID');return {...parseKmlTimeValue(when[0].textContent),kind:'stamp'};}
  const begin=children(primitive,'begin'),end=children(primitive,'end');if(begin.length>1||end.length>1||!begin.length&&!end.length)throw Error('KML_TIME_INVALID');
  const start=begin.length?parseKmlTimeValue(begin[0].textContent).start:-Infinity,finish=end.length?parseKmlTimeValue(end[0].textContent).start:Infinity;
  if(start>=finish)throw Error('KML_TIME_INVALID');
  return {start,end:finish,kind:'span'};
 }catch(error){return {invalid:true,code:error.message};}
}
export function kmlTimeMatches(scope,range){return !range||!scope||!scope.invalid&&scope.start<=range.end&&scope.end>=range.start;}
export function collectKmlTimeScopes(model,overlays={overlays:[]}){
 const features=[],overlayTimes=new Map(),profileSegments=[];let minimum=Infinity,maximum=-Infinity,timed=0,invalid=0;
 const extent=time=>{if(!time)return;if(time.invalid){invalid++;return;}timed++;if(Number.isFinite(time.start))minimum=Math.min(minimum,time.start);if(Number.isFinite(time.end))maximum=Math.max(maximum,time.end);};
 const visibility=node=>!['0','false'].includes(children(node,'visibility')[0]?.textContent.trim()??'1');
 const walk=(node,inherited=null,visible=true)=>{
  if(node.namespaceURI!==NS||!['Document','Folder','Placemark'].includes(node.localName))return;
  const own=readKmlTimePrimitive(node),time=own??inherited;visible&&=visibility(node);
  if(node.localName==='Placemark'){features.push({time,visible});extent(time);}
  else for(const child of node.children)walk(child,time,visible);
 };
 if(model?.xml)for(const node of model.xml.documentElement.children)walk(node);
 if(!model?.xml)for(const feature of model?.features??[])features.push({time:null,visible:feature.visible!==false});
 const allOverlays=Object.fromEntries(['GroundOverlay','ScreenOverlay','PhotoOverlay'].map(type=>[type,model?.xml?[...model.xml.getElementsByTagNameNS(NS,type)]:[]]));
 for(const overlay of overlays.overlays){const node=allOverlays[overlay.type??'GroundOverlay']?.[overlay.domIndex??overlay.index];let time=null;if(node){for(let ancestor=node;ancestor&&ancestor!==model.xml.documentElement;ancestor=ancestor.parentElement){if(ancestor.namespaceURI===NS&&['GroundOverlay','ScreenOverlay','PhotoOverlay','Document','Folder'].includes(ancestor.localName)){const own=readKmlTimePrimitive(ancestor);if(own){time=own;break;}}}}overlayTimes.set(overlay.index,time);extent(time);}
 const geometry=value=>{if(value.geometries){value.geometries.forEach(geometry);return;}if(['LineString','Track'].includes(value.type)&&value.points?.length>1)profileSegments.push(value);if(value.type!=='Track'||!value.time?.some(Number.isFinite))return;timed++;for(const stamp of value.time)if(Number.isFinite(stamp)){minimum=Math.min(minimum,stamp);maximum=Math.max(maximum,stamp);}};
 for(const [index,feature] of (model?.features??[]).entries()){if(features[index])features[index].geometries=feature.geometries??[];for(const value of feature.geometries??[])geometry(value);}
 return {features,overlays:overlayTimes,profileSegments,minimum:Number.isFinite(minimum)?minimum:null,maximum:Number.isFinite(maximum)?maximum:null,timed,invalid};
}
// Keep actual recorded points, split at missing/out-of-window points, never
// interpolate or connect across a gap. Preview objects never replace source/XML.
export function filterKmlTimeGeometry(geometry,range){
 if(!range)return geometry;
 if(geometry.geometries){const children=geometry.geometries.map(child=>filterKmlTimeGeometry(child,range)).filter(Boolean);return children.length?{...geometry,geometries:children}:null;}
 if(geometry.type!=='Track'||!geometry.time?.some(Number.isFinite))return geometry;
 if(geometry.time.length!==geometry.points.length)return null;
 const runs=[];let points=[],time=[];
 const flush=()=>{if(points.length)runs.push(points.length===1?{type:'Point',points,time,altitudeMode:geometry.altitudeMode}:{...geometry,points,time});points=[];time=[];};
 geometry.points.forEach((point,index)=>{const stamp=geometry.time[index];if(Number.isFinite(stamp)&&stamp>=range.start&&stamp<=range.end){points.push(point);time.push(stamp);}else flush();});flush();
 return runs.length?{type:'MultiGeometry',geometries:runs}:null;
}
export function kmlProfilePointVisible(point,scopes,range){
 if(!point)return false;const scope=scopes.features[point.feature];if(scope&&(scope.visible===false||!kmlTimeMatches(scope.time,range)))return false;
 if(!range)return true;
 let time=point.time;
 if(!Number.isFinite(time)&&Number.isInteger(point.segment)&&Number.isInteger(point.index)){const geometry=scopes.profileSegments?.[point.segment];if(geometry?.type==='Track'){time=geometry.time?.[point.index];if(!Number.isFinite(time))return false;}}
 return !Number.isFinite(time)||time>=range.start&&time<=range.end;
}
export function kmlTimeFeatureVisible(scope,range){
 if(!scope||scope.visible===false||!kmlTimeMatches(scope.time,range))return false;
 const matches=geometry=>geometry.geometries?geometry.geometries.some(matches):!range||geometry.type!=='Track'||!geometry.time?.some(Number.isFinite)||geometry.time.some(time=>Number.isFinite(time)&&time>=range.start&&time<=range.end);
 return scope.geometries.some(matches);
}
