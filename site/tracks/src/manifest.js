import {Identity} from '@reticulum/core';
import {validateTrackManifest} from './manifest-schema.js';
import {encode,decode,bytes,integer,equal,validText} from '../../geocaching/src/protocol.js';
const utf8=new TextEncoder(),verified=new WeakMap();
const domains=Object.freeze({signature:utf8.encode('TrailMate.Track.Manifest.v1\0'),object:utf8.encode('TrailMate.Track.ID.v1\0'),revision:utf8.encode('TrailMate.Track.Revision.v1\0')});
const v2=Object.freeze({signature:utf8.encode('TrailMate.Track.Manifest.v2\0'),revision:utf8.encode('TrailMate.Track.Revision.v2\0')});
const concat=(...parts)=>{const result=new Uint8Array(parts.reduce((sum,part)=>sum+part.length,0));let offset=0;for(const part of parts){result.set(part,offset);offset+=part.length;}return result;};
export {validateTrackManifest} from './manifest-schema.js';
export function encodeTrackManifest(record){const encoded=encode(validateTrackManifest(record));if(encoded.length>4096)throw Error('轨迹清单超过容量限制');return encoded;}
export function trackManifestSigningBytes(manifest){const record=validateTrackManifest(decode(manifest,4096));return concat(record[0]===2?v2.signature:domains.signature,manifest);}
export async function verifyTrackManifest(signed,{expectedAuthor}={}){
 if(!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64))throw Error('轨迹签名清单无效');
 if(expectedAuthor!==undefined&&!bytes(expectedAuthor,64))throw Error('预期作者身份无效');
 const manifest=signed[0].slice(),signature=signed[1].slice(),expected=expectedAuthor?.slice();
 const record=validateTrackManifest(decode(manifest,4096));
 if(expected&&!equal(expected,record[1]))throw Error('轨迹清单不属于预期设备身份');
 const identity=await Identity.fromPublicKey(record[1]);
 if(!await identity.validate(signature,concat(record[0]===2?v2.signature:domains.signature,manifest)))throw Error('轨迹作者签名验证失败');
 const objectId=await Identity.fullHash(concat(domains.object,record[1],record[2])),revisionHash=await Identity.fullHash(concat(record[0]===2?v2.revision:domains.revision,manifest));
 const result=Object.freeze({record:decode(manifest,4096),manifest:manifest.slice(),signature:signature.slice(),objectId:objectId.slice(),revisionHash:revisionHash.slice()});
 verified.set(result,{record,objectId,revisionHash});return result;
}
export function verifyTrackSuccessor(previous,next){
 const parent=verified.get(previous),child=verified.get(next);if(!parent||!child)throw Error('版本链需要先验证作者签名');
 const a=parent.record,b=child.record;
 if(!equal(parent.objectId,child.objectId)||b[4]!==a[4]+1||!equal(b[5],parent.revisionHash)||b[7]!==a[7]||b[8]<a[8]||a[6]===1&&b[6]===0||b[0]<a[0])throw Error('轨迹版本链不一致');
 if(b[6]===1&&(!equal(a[13][0],b[13][0])||a[13][1]!==b[13][1]||a[13][2]!==b[13][2]||a[13][3]!==b[13][3]))throw Error('归档版本必须保留原文件');
 if(b[6]===1&&b[0]===2&&!equal(encode(a[0]===2?a[15]:null),encode(b[15])))throw Error('归档版本必须保留原附件');
 return true;
}
