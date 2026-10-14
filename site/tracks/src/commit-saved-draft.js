import {readTrackDraft,saveTrackDraftJobBinding} from './draft-store.js';
import {decodeTrackPublicationPlan} from './publication-plan.js';
import {commitTrack} from './device-commit.js';
import {validatePreparedPlanBinding} from './prepared-plan-binding.js';
import {bytes,integer} from '../../geocaching/src/protocol.js';
const nonzero=(value,size)=>bytes(value,size)&&value.some(byte=>byte);

export async function commitSavedTrackDraft(session,id,expectedVersion,prepared,directory,author,options={}){
 if(!session||typeof session.request!=='function'||!author?.verified||!nonzero(author.publicKey,64)||
  !integer(author.generation,1,0xffffffff)||prepared?.prepared!==true||prepared.generation!==author.generation||
  !nonzero(prepared.operationId,16)||!nonzero(prepared.objectId,32)||!nonzero(prepared.revisionHash,32)||
  !integer(prepared.revision,1,0xffffffff)||!directory?.capabilitiesVerified||directory.protocol!=='trailmate.track.v1'||
  !nonzero(directory.delivery,16)||!nonzero(directory.epoch,16))throw Error('提交需要当前设备身份和已验证目录的准备结果');
 const retainedPrepared={...prepared,operationId:prepared.operationId.slice(),objectId:prepared.objectId.slice(),revisionHash:prepared.revisionHash.slice()};
 const retainedDirectory={...directory,delivery:directory.delivery.slice(),epoch:directory.epoch.slice()};
 const retainedAuthor={...author,publicKey:author.publicKey.slice()};
 const check=()=>{if(options.signal?.aborted)throw options.signal.reason??new DOMException('轨迹提交等待已暂停','AbortError');};
 check();const draft=await readTrackDraft(id);check();
 if(draft.version!==expectedVersion||!draft.publicationPlan)throw Error('草稿或准备计划已变化，请重新打开');
 const currentHash=new Uint8Array(draft.hash.match(/../g).map(value=>parseInt(value,16)));
 const plan=await validatePreparedPlanBinding(decodeTrackPublicationPlan(new Uint8Array(draft.publicationPlan)),retainedPrepared,retainedAuthor,currentHash,draft.bytes);check();
 const binding={operationId:retainedPrepared.operationId,objectId:retainedPrepared.objectId,revisionHash:retainedPrepared.revisionHash,
  generation:retainedAuthor.generation,revision:retainedPrepared.revision,directory:retainedDirectory.delivery,directoryEpoch:retainedDirectory.epoch,
  authorHash:new Uint8Array(await crypto.subtle.digest('SHA-256',retainedAuthor.publicKey)),fileHash:plan.fileHash,fileBytes:plan.fileBytes};
 check();await saveTrackDraftJobBinding(id,expectedVersion,binding);check();
 const receipt=await commitTrack(session,retainedPrepared,retainedDirectory,retainedAuthor,options);
 return {...binding,...receipt};
}
