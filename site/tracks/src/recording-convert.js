// These layouts match the existing ESP writers, including their ABI padding.
// Conversion stays in the browser; no device point array is required.
const LIMIT_BYTES=8*1024*1024,LIMIT_POINTS=100000;
const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
const escape=value=>String(value).replace(/[&<>"']/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[char]));
function numeric(raw){if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(raw.trim()))throw Error('记录数值格式无效');const value=Number(raw);if(!Number.isFinite(value))throw Error('记录数值不是有限数');return value;}
function integer(raw,max){const value=numeric(raw);if(!Number.isInteger(value)||value<0||value>max)throw Error('记录整数超出范围');return value;}
function epoch(seconds){return seconds>=1577836800?new Date(seconds*1000).toISOString():null;}
function checkPoint(point){if(Math.abs(point.lat)>90||Math.abs(point.lon)>180)throw Error('记录经纬度超出范围');return point;}
function csvPoints(raw){
 const lines=decoder.decode(raw).replace(/^\uFEFF/,'').split(/\r?\n/);const header=lines.shift();
 const arduino=header==='lat,lon,ts,sat',idf=header==='time,lat,lon,alt_m,speed_mps,satellites';
 if(!arduino&&!idf)throw Error('不支持此 CSV 记录列格式');
 const points=[];
 for(let index=0;index<lines.length;index++){
  if(!lines[index].trim()){if(lines.slice(index).some(line=>line.trim()))throw Error('CSV 记录中间存在空行');break;}
  const fields=lines[index].split(',');if(fields.length!==(arduino?4:6))throw Error('CSV 记录列数不匹配');
  if(points.length>=LIMIT_POINTS)throw Error('记录点数量超过限制');
  let point;
  if(arduino){const seconds=integer(fields[2],0xffffffff);point={lat:numeric(fields[0]),lon:numeric(fields[1]),alt:null,time:epoch(seconds),timestamp_s:seconds,satellites:integer(fields[3],255)};}
  else{
   const text=fields[0];let time=null;
   if(text){if(!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(text)||!Number.isFinite(Date.parse(text))||new Date(Date.parse(text)).toISOString().slice(0,19)+'Z'!==text)throw Error('CSV 时间无效');time=text;}
   const speed=numeric(fields[4]);if(speed<0)throw Error('CSV 速度不能为负');
   point={lat:numeric(fields[1]),lon:numeric(fields[2]),alt:null,time,recorded_alt_m:numeric(fields[3]),recorded_speed_mps:speed,satellites:integer(fields[5],255)};
  }
  points.push(checkPoint(point));
 }
 return {points,profile:arduino?'arduino-csv-v1':'idf-csv-v1',issues:[idf?'此 CSV 未记录高度/速度有效标记；原数值保留，但不作为已知海拔或速度。':'此记录格式没有高度。']};
}
function binaryPoints(raw){
 const prefix=String.fromCharCode(...raw.subarray(0,6));const arduino=prefix.startsWith('TRK1'),idf=prefix==='TMTRK1';
 if(!arduino&&!idf)throw Error('二进制记录头无效');
 const start=arduino?4:6,stride=arduino?16:20;
 if((raw.length-start)%stride)throw Object.assign(Error('二进制记录截断或布局不匹配'),{code:'RECORDING_BINARY_TRUNCATED'});
 const count=(raw.length-start)/stride;if(count>LIMIT_POINTS)throw Error('记录点数量超过限制');
 const view=new DataView(raw.buffer,raw.byteOffset,raw.byteLength),points=[];
 for(let offset=start;offset<raw.length;offset+=stride){
  const seconds=view.getUint32(offset+(arduino?8:0),true),flags=arduino?0:view.getUint8(offset+17);
  if(flags&~3)throw Error('二进制记录存在未知有效标记');
  points.push(checkPoint({lat:view.getInt32(offset+(arduino?0:4),true)/1e7,lon:view.getInt32(offset+(arduino?4:8),true)/1e7,alt:flags&1?view.getInt16(offset+12,true):null,time:epoch(seconds),timestamp_s:seconds,speed_mps:flags&2?view.getUint16(offset+14,true)/100:null,satellites:view.getUint8(offset+(arduino?12:16))}));
 }
 return {points,profile:arduino?'arduino-trk1-le16':'idf-tmtrk1-le20',issues:arduino?['此记录格式没有高度。']:[]};
}
export function convertRecording(input,{format,title='设备轨迹'}={}){
 if(!(input instanceof Uint8Array)||!input.length||input.length>LIMIT_BYTES)throw Error('记录文件超过容量限制');
 if(typeof title!=='string'||!title.trim()||encoder.encode(title).length>128)throw Error('轨迹名称需要 1–128 字节');
 const source=input.slice();const result=format==='csv'?csvPoints(source):format==='binary'?binaryPoints(source):null;
 if(!result)throw Error('此转换入口仅支持设备 CSV 和二进制记录');
 const {points,profile,issues}=result;if(!points.length)throw Error('记录没有坐标点');
 const timed=points.every(point=>point.time!==null&&point.alt!==null);
 const coordinate=point=>`${point.lon},${point.lat}${point.alt===null?'':','+point.alt}`;
 let geometry;
 if(timed)geometry=`<gx:Track><altitudeMode>absolute</altitudeMode>${points.map(point=>'<when>'+point.time+'</when>').join('')}${points.map(point=>`<gx:coord>${point.lon} ${point.lat} ${point.alt}</gx:coord>`).join('')}</gx:Track>`;
 else if(points.length===1)geometry=`<Point>${points[0].alt===null?'':'<altitudeMode>absolute</altitudeMode>'}<coordinates>${coordinate(points[0])}</coordinates></Point>`;
 else geometry=`<LineString>${points.every(point=>point.alt!==null)?'<altitudeMode>absolute</altitudeMode>':''}<coordinates>${points.map(coordinate).join(' ')}</coordinates></LineString>`;
 if(!timed&&points.some(point=>point.time!==null))issues.push('时间按点保存在 ExtendedData 中；缺少高度时不为 gx:Track 填造零高度。');
 if(points.some(point=>point.time===null))issues.push('部分时间没有有效 UTC；原设备计数保留，不推断日期。');
 const metadata=points.map(({lat,lon,alt,...data})=>data);
 const xml=`<?xml version="1.0" encoding="UTF-8"?><kml xmlns="http://www.opengis.net/kml/2.2" xmlns:gx="http://www.google.com/kml/ext/2.2"><Document><name>${escape(title)}</name><Placemark><name>${escape(title)}</name><ExtendedData><Data name="trailmate.recording.profile"><value>${profile}</value></Data><Data name="trailmate.recording.warnings"><value>${escape(issues.join('\n'))}</value></Data><Data name="trailmate.recording.point_metadata"><value>${escape(JSON.stringify(metadata))}</value></Data></ExtendedData>${geometry}</Placemark></Document></kml>`;
 const kml=encoder.encode(xml);if(kml.length>LIMIT_BYTES)throw Error('转换后的 KML 超过容量限制');
 return {source,kml,profile,pointCount:points.length,issues};
}
