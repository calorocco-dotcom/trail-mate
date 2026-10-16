import {readTrackDraft,saveTrackDraftPublicationPlan} from './draft-store.js';
import {createTrackPublicationPlan} from './publication-create.js';
import {decodeTrackPublicationPlan,validateTrackPublicationResume} from './publication-plan.js';
import {prepareTrackPublication} from './device-publication-prepare.js';
import {bytes,integer,decode} from '../../geocaching/src/protocol.js';
import {createTrackSuccessorPlan} from './publication-successor.js';
import {draftPackageCreationOptions,draftPackageInputs} from './draft-package.js';

export async function prepareSavedTrackDraft(session,id,expectedVersion,author,{signal,onProgress=()=>{},
  creationOptions={},prepareOptions={},operations=[]}={}){
  if(!session||typeof session.request!=='function'||!Number.isSafeInteger(expectedVersion)||expectedVersion<1)throw Error('轨迹准备会话或草稿版本无效');
  if(!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff))throw Error('轨迹准备需要已验证的设备作者身份');
  const retainedAuthor={verified:true,publicKey:author.publicKey.slice(),generation:author.generation};
  const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('轨迹准备已暂停','AbortError');};
  check();const snapshot=await readTrackDraft(id);check();
  if(snapshot.version!==expectedVersion)throw Error('轨迹草稿已变化，请重新打开后准备');
  const fileHash=new Uint8Array(snapshot.hash.match(/../g).map(value=>parseInt(value,16)));
  let plan;
  const packageOptions=await draftPackageCreationOptions(snapshot);check();
  if(snapshot.publicationPlan)plan=validateTrackPublicationResume(decodeTrackPublicationPlan(new Uint8Array(snapshot.publicationPlan)),retainedAuthor,fileHash,snapshot.bytes);
  else if(snapshot.predecessor)plan=await createTrackSuccessorPlan(snapshot.source,retainedAuthor,decode(new Uint8Array(snapshot.predecessor),4352),{...creationOptions,...packageOptions,archive:snapshot.archive===true,title:snapshot.name,signal});
  else plan=await createTrackPublicationPlan(snapshot.source,retainedAuthor,{...creationOptions,...packageOptions,title:snapshot.name});
  check();
  // The transaction checks the version again after asynchronous hashing. Serial
  // traffic starts only after the exact retry descriptor is durable.
  await saveTrackDraftPublicationPlan(id,expectedVersion,plan);check();
  const attachments=await draftPackageInputs(snapshot,plan);check();
  return prepareTrackPublication(session,{...plan,...attachments,kml:snapshot.source,author:retainedAuthor},{signal,onProgress,prepareOptions,operations});
}
