import {bytes,integer,validText} from '../../geocaching/src/protocol.js';
import {validateTrackMetadata,validateAttachmentIndexDescriptor} from './manifest-extensions.js';
export function validateTrackManifest(record){
 if(!Array.isArray(record)||![1,2].includes(record[0])||record.length!==(record[0]===1?14:16)||!bytes(record[1],64)||!record[1].some(byte=>byte)||!bytes(record[2],16)||!record[2].some(byte=>byte)||!integer(record[3],0,1)||!integer(record[4],1,0xffffffff)||!integer(record[6],0,1))throw Error('轨迹清单身份或版本无效');
 if(record[4]===1?record[5]!==null:!bytes(record[5],32))throw Error('轨迹清单父版本无效');
 if(record[4]===1&&record[6]!==0)throw Error('轨迹首版必须为活动状态');
 if(!integer(record[7],0,253402300799)||!integer(record[8],record[7],253402300799)||!validText(record[9],128,false,true)||!validText(record[10],2048,true))throw Error('轨迹清单时间或文本无效');
 const box=record[11],stats=record[12],file=record[13];
 if(!Array.isArray(box)||box.length!==4||!integer(box[0],-1800000000,1799999999)||!integer(box[2],-1800000000,1799999999)||!integer(box[1],-900000000,900000000)||!integer(box[3],box[1],900000000))throw Error('轨迹清单地理范围无效');
 if(!Array.isArray(stats)||stats.length!==4||!integer(stats[0],0,0xffffffff)||![stats[1],stats[2]].every(value=>value===null||integer(value,0,0xffffffff))||!integer(stats[3],1,100000))throw Error('轨迹清单统计无效');
 if(!Array.isArray(file)||file.length!==4||!bytes(file[0],32)||!integer(file[1],1,8388608)||file[2]!=='application/vnd.google-earth.kml+xml'||file[3]!==1)throw Error('轨迹清单文件约束无效');
 if(record[0]===2){validateTrackMetadata(record[14],stats[3]);validateAttachmentIndexDescriptor(record[15]);}
 return record;
}
