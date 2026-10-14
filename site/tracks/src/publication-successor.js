import {bytes,integer,equal,encode} from '../../geocaching/src/protocol.js';
import {parseKml} from './kml.js';
import {manifestGeometryBounds} from './manifest-geometry.js';
import {summarizeKml} from './kml-summary.js';
import {encodeTrackManifest,verifyTrackManifest} from './manifest.js';
import {encodeTrackPublicationPlan,decodeTrackPublicationPlan} from './publication-plan.js';
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};

// The signed predecessor proves authorship, not that it is still the directory
// head. Preparation/publication must also check the exact predecessor hash.
export async function createTrackSuccessorPlan(kml,author,previousSigned,{archive=false,title,description,assets,metadata,
 updatedAt=Math.floor(Date.now()/1000),randomBytes=size=>crypto.getRandomValues(new Uint8Array(size)),signal}={}){
 check(signal);
 if(!(kml instanceof Uint8Array)||!kml.length||kml.length>8388608||!author?.verified||!bytes(author.publicKey,64)||!integer(author.generation,1,0xffffffff)||typeof archive!=='boolean'||typeof randomBytes!=='function')throw Error('后续版本需要设备身份、已签名父版本与 KML');
 const content=kml.slice(),publicKey=author.publicKey.slice(),generation=author.generation;
 const previous=await verifyTrackManifest(previousSigned,{expectedAuthor:publicKey});check(signal);
 const record=previous.record;
 if(record[6]!==0||record[4]===0xffffffff||!integer(updatedAt,record[8],253402300799))throw Error('父版本已归档、版本号耗尽或更新时间回退');
 const fileHash=new Uint8Array(await crypto.subtle.digest('SHA-256',content));check(signal);
 if(archive){
  if(content.length!==record[13][1]||fileHash.some((value,index)=>value!==record[13][0][index]))throw Error('归档必须保留原 KML 文件');
 }else{
  const model=parseKml(content),geometry=manifestGeometryBounds(model),summary=summarizeKml(model);
  record[11]=geometry.bbox;record[12]=[Math.round(summary.distanceM??0),summary.durationS===null?null:Math.round(summary.durationS),summary.ascentM===null?null:Math.round(summary.ascentM),geometry.pointCount];
  record[13]=[fileHash,content.length,'application/vnd.google-earth.kml+xml',1];
 }
 record[4]++;record[5]=previous.revisionHash.slice();record[6]=archive?1:0;record[8]=updatedAt;
 if(archive){
  if(assets!==undefined&&!equal(encode(assets),encode(record[0]===2?record[15]:null)))throw Error('Archive must retain original attachments');
 }else if(record[0]===2||assets!==undefined&&assets!==null||metadata!==undefined){
  if(record[0]===1){record[0]=2;record.push(metadata??[0,[],0,0,0,0,0,null],assets??null);}
  else {if(metadata!==undefined)record[14]=metadata;if(assets!==undefined)record[15]=assets;}
 }
 if(title!==undefined)record[9]=title;if(description!==undefined)record[10]=description;
 const manifest=encodeTrackManifest(record);check(signal);
 const operationId=randomBytes(16).slice(),kmlUploadId=randomBytes(16).slice(),manifestUploadId=randomBytes(16).slice();
 return decodeTrackPublicationPlan(encodeTrackPublicationPlan({operationId,kmlUploadId,manifestUploadId,generation,manifest,fileHash,fileBytes:content.length}));
}
