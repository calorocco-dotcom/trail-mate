import {bytes,equal,integer,validText} from './protocol.js';
import {OPERATION,MANAGEMENT_STATUS} from '../../device/src/management-protocol.js';

const max64=0xffffffffffffffffn;
const uint64=v=>typeof v==='bigint'?v>=0n&&v<=max64:Number.isSafeInteger(v)&&v>=0;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));

export function decodeDeviceDraftRow(row,authorKey){
  if(!Array.isArray(row)||row.length!==9)throw Error('设备草稿目录行无效');
  const [id,generation,author,name,state,latitudeE7,longitudeE7,baseHash,publication]=row;
  if(!bytes(id,16)||!uint64(generation)||BigInt(generation)===0n||(author!==null&&!bytes(author,64))||
     !validText(name,96)||!integer(state,0,2)||(latitudeE7===null)!==(longitudeE7===null)||
     (latitudeE7!==null&&(!integer(latitudeE7,-900000000,900000000)||!integer(longitudeE7,-1800000000,1799999999)))||
     (baseHash!==null&&!bytes(baseHash,32))||!Array.isArray(publication)||publication.length!==3||
     !integer(publication[0],0,0xffffffff)||!integer(publication[1],0,publication[0])||!integer(publication[2],0,15))throw Error('设备草稿目录行无效');
  const [latestRevision,confirmedRevision,flags]=publication;
  return {id:id.slice(),generation,authorKey:author?.slice()??null,name,state,latitudeE7,longitudeE7,
    baseHash:baseHash?.slice()??null,editable:author===null||equal(author,authorKey),
    publication:{latestRevision,confirmedRevision,pending:Boolean(flags&1),stopped:Boolean(flags&2),localChanges:Boolean(flags&4),baseRetained:Boolean(flags&8)}};
}

export async function readDeviceDraftPage(session,author,{offset=0,limit=4,catalogGeneration=null,signal,
  timeoutMs=30000,retryMs=100,sleep=delay,now=()=>Date.now()}={}){
  if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Error('读取草稿目录前必须验证设备作者身份');
  if(!integer(offset,0,0xffffffff)||!integer(limit,1,16)||offset+limit>0xffffffff||
     (catalogGeneration!==null&&!uint64(catalogGeneration))||(offset&&catalogGeneration===null))throw Error('草稿目录分页参数无效');
  if(!Number.isFinite(timeoutMs)||timeoutMs<=0||!Number.isFinite(retryMs)||retryMs<=0)throw Error('草稿目录读取时间预算无效');
  const authorKey=author.publicKey.slice(),authorGeneration=author.generation,deadline=now()+timeoutMs;
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('草稿目录读取已取消','AbortError');if(now()>=deadline)throw Error('草稿目录读取超时');};
  let generation=catalogGeneration,knownCount=0,ended=false;const rows=[],seen=new Set();
  while(rows.length<limit){
    check();const cursor=offset+rows.length;let reply;
    try{reply=await session.request(OPERATION.CACHE_LIST_OWNED,[cursor,generation,authorGeneration]);}
    catch(error){check();if(error.code!==MANAGEMENT_STATUS.BUSY)throw error;await sleep(Math.min(retryMs,Math.max(1,deadline-now())));continue;}
    check();
    if(!Array.isArray(reply)||reply.length!==4||!uint64(reply[0])||reply[1]!==cursor||!integer(reply[2],0,0xffffffff))throw Error('设备草稿目录响应无效');
    if(generation!==null&&BigInt(generation)!==BigInt(reply[0]))throw Error('设备草稿目录在翻页期间变化，请刷新列表');
    generation=reply[0];knownCount=Math.max(knownCount,reply[2]);
    if(reply[3]===null){if(reply[2]>cursor)throw Error('设备草稿目录结束标记无效');ended=true;break;}
    if(reply[2]<=cursor)throw Error('设备草稿目录数量下界无效');
    const row=decodeDeviceDraftRow(reply[3],authorKey),key=Array.from(row.id,b=>b.toString(16).padStart(2,'0')).join('');
    if(seen.has(key))throw Error('设备草稿目录包含重复条目');seen.add(key);rows.push(row);
  }
  check();
  return {rows,catalogGeneration:generation,knownCount,nextOffset:offset+rows.length,hasMore:!ended};
}
