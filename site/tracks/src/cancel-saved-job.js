import {readTrackDraft,readTrackJobHistory} from './draft-store.js';
import {decodeTrackJobBinding} from './job-binding.js';
import {cancelTrackJob} from './cancel-job.js';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
export async function cancelSavedTrackJob(session,{historyKey,draftId,draftVersion},author,options={}){
 if(!author?.verified||!bytes(author.publicKey,64)||!author.publicKey.some(v=>v)||!integer(author.generation,1,0xffffffff))throw Error('取消需要已验证的设备作者身份');
 const retained={publicKey:author.publicKey.slice(),generation:author.generation};
 const check=()=>{if(options.signal?.aborted)throw options.signal.reason??new DOMException('取消已暂停','AbortError');};
 check();let binding;
 if(historyKey)binding=(await readTrackJobHistory(historyKey)).binding;
 else{const draft=await readTrackDraft(draftId);if(draft.version!==draftVersion||!draft.jobBinding)throw Error('任务记录或草稿版本已变化');binding=decodeTrackJobBinding(new Uint8Array(draft.jobBinding));}
 check();const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',retained.publicKey));check();
 if(retained.generation!==binding.generation||!equal(hash,binding.authorHash))throw Error('设备身份已变化，不能取消原轨迹任务');
 return cancelTrackJob(session,binding,options);
}
