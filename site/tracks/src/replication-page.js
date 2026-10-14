import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
const watermark=value=>typeof value==='bigint'?value>=0n&&value<=0x7fffffffffffffffn:integer(value,0,Number.MAX_SAFE_INTEGER);
const fail=()=>{throw Error('Invalid track replication inventory');};

export function validateTrackReplicationPage(response,{after=0,high=null,limit=16}={}){
 if(!watermark(after)||high!==null&&(!watermark(high)||BigInt(high)<BigInt(after))||!integer(limit,1,32))fail();
 if(!Array.isArray(response)||response.length!==4||!watermark(response[0])||!watermark(response[1])||!integer(response[2],0,1)||!Array.isArray(response[3])||response[3].length>limit||BigInt(response[0])<BigInt(after)||high!==null&&BigInt(response[0])!==BigInt(high))fail();
 let last=BigInt(after);const items=[];
 for(const row of response[3]){
  if(!Array.isArray(row)||row.length!==9||!watermark(row[0])||BigInt(row[0])<=last||BigInt(row[0])>BigInt(response[0])||!bytes(row[1],32)||!bytes(row[2],32)||!integer(row[3],1,0xffffffff)||!integer(row[4],0,1)||row[3]===1&&(row[5]!==null||row[4]!==0)||row[3]>1&&(!bytes(row[5],32)||!row[5].some(Boolean))||!bytes(row[6],32)||!integer(row[7],1,8388608)||!integer(row[8],1,2))fail();
  items.push(Object.freeze({sequence:row[0],objectId:row[1].slice(),revisionHash:row[2].slice(),revision:row[3],state:row[4],parent:row[5]?.slice()??null,fileHash:row[6].slice(),fileBytes:row[7],manifestVersion:row[8]}));last=BigInt(row[0]);
 }
 if(response[2]&&(!items.length||BigInt(response[1])!==last||last>=BigInt(response[0]))||!response[2]&&BigInt(response[1])!==BigInt(response[0]))fail();
 return Object.freeze({high:response[0],next:response[1],more:response[2]===1,items:Object.freeze(items)});
}

export function matchTrackReplicationItem(item,verified){
 const record=verified?.record;
 if(!item||!Array.isArray(record)||!equal(item.objectId,verified.objectId)||!equal(item.revisionHash,verified.revisionHash)||item.manifestVersion!==record[0]||item.revision!==record[4]||item.state!==record[6]||!(item.parent===null&&record[5]===null||bytes(item.parent,32)&&equal(item.parent,record[5]))||!Array.isArray(record[13])||!equal(item.fileHash,record[13][0])||item.fileBytes!==record[13][1])fail();
 return true;
}
