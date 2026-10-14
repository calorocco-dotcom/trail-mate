import {readTrackDraft,saveTrackDraftPublicationPlan} from './draft-store.js';
import {decodeTrackPublicationPlan,validateTrackPublicationResume} from './publication-plan.js';
import {createTrackSuccessorPlan} from './publication-successor.js';
import {verifyTrackManifest} from './manifest.js';
import {prepareTrackPublication} from './device-publication-prepare.js';
import {bytes,equal,integer,decode} from '../../geocaching/src/protocol.js';

export async function prepareSavedTrackSuccessor(session,id,expectedVersion,author,previousSigned,{archive=false,signal,onProgress=()=>{},creationOptions={},prepareOptions={}}={}){
 if(!session||typeof session.request!=='function'||!integer(expectedVersion,1,Number.MAX_SAFE_INTEGER)||!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff)||typeof archive!=='boolean')throw Error('后续版本准备参数无效');
 const retainedAuthor={verified:true,publicKey:author.publicKey.slice(),generation:author.generation};
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
 check();const snapshot=await readTrackDraft(id);check();
 if(snapshot.version!==expectedVersion)throw Error('轨迹草稿已变化，请重新打开');
 const previous=await verifyTrackManifest(previousSigned,{expectedAuthor:retainedAuthor.publicKey});check();
 if(previous.record[6]!==0)throw Error('已归档轨迹不能继续准备后续版本');
 const fileHash=new Uint8Array(snapshot.hash.match(/../g).map(value=>parseInt(value,16)));let plan;
 if(snapshot.publicationPlan){
  plan=validateTrackPublicationResume(decodeTrackPublicationPlan(new Uint8Array(snapshot.publicationPlan)),retainedAuthor,fileHash,snapshot.bytes);
  const record=decode(plan.manifest,4096);
  if(record[4]!==previous.record[4]+1||!equal(record[5],previous.revisionHash)||!equal(record[2],previous.record[2])||record[3]!==previous.record[3]||record[7]!==previous.record[7]||record[8]<previous.record[8]||record[6]!==Number(archive))throw Error('已有准备计划与所选父版本或更新操作不一致');
  if(archive&&(!equal(record[13][0],previous.record[13][0])||record[13][1]!==previous.record[13][1]||record[11].some((value,index)=>value!==previous.record[11][index])||record[12].some((value,index)=>value!==previous.record[12][index])))throw Error('归档重试计划必须保留原文件、范围和统计');
 }else plan=await createTrackSuccessorPlan(snapshot.source,retainedAuthor,previousSigned,{...creationOptions,archive,title:creationOptions.title??snapshot.name,signal});
 check();await saveTrackDraftPublicationPlan(id,expectedVersion,plan);check();
 return prepareTrackPublication(session,{...plan,kml:snapshot.source,author:retainedAuthor},{signal,onProgress,prepareOptions});
}
