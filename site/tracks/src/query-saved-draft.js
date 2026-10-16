import {readTrackDraft} from './draft-store.js';
import {decodeTrackJobBinding} from './job-binding.js';
import {waitTrackJob} from './wait-track-job.js';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';

export async function querySavedTrackDraft(session,id,expectedVersion,author,options={}){
 if(!author?.verified||!bytes(author.publicKey,64)||!author.publicKey.some(v=>v)||!integer(author.generation,1,0xffffffff))
  throw Error('查询需要已验证的设备作者身份');
 const retainedAuthor={publicKey:author.publicKey.slice(),generation:author.generation};
 const check=()=>{if(options.signal?.aborted)throw options.signal.reason??new DOMException('已暂停任务查询','AbortError');};
 check();const draft=await readTrackDraft(id);check();
 if(draft.version!==expectedVersion||!draft.jobBinding)throw Error('任务记录缺失或草稿版本已变化');
 const binding=decodeTrackJobBinding(new Uint8Array(draft.jobBinding));
 const authorHash=new Uint8Array(await crypto.subtle.digest('SHA-256',retainedAuthor.publicKey));check();
 if(binding.generation!==retainedAuthor.generation||!equal(binding.authorHash,authorHash))throw Error('设备身份已变化，不能查询原轨迹任务');
 return waitTrackJob(session,binding,options);
}
