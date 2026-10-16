import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';
import {integer} from '../../geocaching/src/protocol.js';
const encoder=new TextEncoder(),MAX_POSITION=128*1024*1024,MAX_U64=(1n<<64n)-1n;
function size(value){if(typeof value==='bigint'&&value>=0n&&value<=MAX_U64)return value;if(Number.isSafeInteger(value)&&value>=0)return BigInt(value);throw Error('设备记录文件大小无效');}
function cursorValid(value){return value&&integer(value.media,0,0xffffffff)&&integer(value.position,0,MAX_POSITION)&&value.position%32===0;}
export async function readDeviceTrackCatalogPage(session,cursor=null,{signal}={}){
 if(cursor!==null&&!cursorValid(cursor))throw Error('设备轨迹目录游标无效');
 if(signal?.aborted)throw signal.reason??new DOMException('读取已取消','AbortError');
 const requested=cursor===null?null:Object.freeze([cursor.media,cursor.position]);
 let result;try{result=await session.request(OPERATION.TRACK_LIST,[requested]);}catch(error){if(error.code==='CONFLICT'||error.code===MANAGEMENT_STATUS.CONFLICT)throw Object.assign(Error('设备记录列表已变化，请刷新'),{code:'CATALOG_CHANGED'});if(error.code==='BUSY'||error.code===MANAGEMENT_STATUS.BUSY)throw Object.assign(Error('设备正在读取其他数据，请稍后重试'),{code:'CATALOG_BUSY'});throw error;}
 if(signal?.aborted)throw signal.reason??new DOMException('读取已取消','AbortError');
 if(!Array.isArray(result)||result.length!==3||!integer(result[0],0,0xffffffff))throw Error('设备轨迹目录响应无效');
 const [media,next,raw]=result;
 if(requested!==null&&requested[0]!==media)throw Object.assign(Error('设备存储会话已变化，请刷新记录列表'),{code:'CATALOG_CHANGED'});
 if(next!==null&&(!integer(next,1,MAX_POSITION)||next%32||next<=(requested?.[1]??0)))throw Error('设备轨迹目录游标未推进');
 if(raw!==null&&next===null)throw Error('设备轨迹目录结束标记无效');
 let entry=null;
 if(raw!==null){
  if(!Array.isArray(raw)||raw.length!==4||typeof raw[0]!=='string'||!raw[0]||encoder.encode(raw[0]).length>127||/[\x00-\x1f/\\:]/.test(raw[0])||!integer(raw[1],0,3)||!integer(raw[3],0,1))throw Error('设备轨迹记录格式无效');
  const format={kml:0,gpx:1,csv:2,bin:3}[raw[0].split('.').at(-1).toLowerCase()];if(format!==raw[1]||raw[0].toLowerCase()==='active.bin')throw Error('设备轨迹记录类型不匹配');
  const bytes=size(raw[2]);entry=Object.freeze({name:raw[0],format,bytes,recording:raw[3]===1});
 }
 return Object.freeze({media,cursor:next===null?null:Object.freeze({media,position:next}),entry});
}
