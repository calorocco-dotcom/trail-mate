import {readTrackJobHistory} from './draft-store.js';
import {waitTrackJob} from './wait-track-job.js';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
export async function queryTrackHistory(session,key,author,options={}){
 if(!author?.verified||!bytes(author.publicKey,64)||!author.publicKey.some(v=>v)||!integer(author.generation,1,0xffffffff))throw Error('查询需要已验证的设备作者身份');
 const retained={publicKey:author.publicKey.slice(),generation:author.generation};
 const check=()=>{if(options.signal?.aborted)throw options.signal.reason??new DOMException('已暂停历史查询','AbortError');};
 check();const record=await readTrackJobHistory(key);check();
 const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',retained.publicKey));check();
 if(retained.generation!==record.binding.generation||!equal(hash,record.binding.authorHash))throw Error('设备身份已变化，不能查询原轨迹任务');
 return waitTrackJob(session,record.binding,options);
}
