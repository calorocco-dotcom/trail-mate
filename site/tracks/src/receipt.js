import {Identity} from '@reticulum/core';
import {bytes,equal,integer,decode} from '../../geocaching/src/protocol.js';
import {verifyTrackManifest} from './manifest.js';
const utf8=new TextEncoder(),domain=utf8.encode('TrailMate.Track.Receipt.v1\0');
const concat=(...parts)=>{const out=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let pos=0;for(const p of parts){out.set(p,pos);pos+=p.length;}return out;};

// Pins caller-verified directory context before awaits. This establishes that
// the selected node accepted this exact version, not permanent latest status.
export async function verifyTrackReceipt(signed,{directoryPublicKey,directoryEpoch,requestId,manifestSigned}={}){
 if(!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!signed[0].length||signed[0].length>1024||!bytes(signed[1],64)||
    !bytes(directoryPublicKey,64)||!bytes(directoryEpoch,16)||!bytes(requestId,16)||!Array.isArray(manifestSigned)||manifestSigned.length!==2||
    !(manifestSigned[0] instanceof Uint8Array)||!bytes(manifestSigned[1],64))throw Error('轨迹目录回执上下文无效');
 const raw=signed[0].slice(),signature=signed[1].slice(),key=directoryPublicKey.slice(),epoch=directoryEpoch.slice(),request=requestId.slice();
 const manifest=[manifestSigned[0].slice(),manifestSigned[1].slice()],record=decode(raw,1024);
 if(!Array.isArray(record)||record.length!==13||record[0]!==1||!bytes(record[1],16)||!bytes(record[2],32)||!bytes(record[3],32)||
    !integer(record[4],1,0xffffffff)||!integer(record[5],0,1)||!bytes(record[6],32)||!integer(record[7],1,8388608)||
    !bytes(record[8],64)||!bytes(record[9],16)||!bytes(record[10],16)||record[11]!==1||!integer(record[12],0,253402300799))throw Error('轨迹目录回执字段无效');
 const version=await verifyTrackManifest(manifest),identity=await Identity.fromPublicKey(key);
 const nameHash=(await Identity.fullHash(utf8.encode('lxmf.delivery'))).slice(0,10);
 const delivery=await Identity.truncatedHash(concat(nameHash,identity.identityHash));
 if(!equal(record[1],request)||!equal(record[2],version.objectId)||!equal(record[3],version.revisionHash)||record[4]!==version.record[4]||
    record[5]!==version.record[6]||!equal(record[6],version.record[13][0])||record[7]!==version.record[13][1]||
    !equal(record[8],key)||!equal(record[9],delivery)||!equal(record[10],epoch))throw Error('轨迹目录回执绑定不一致');
 if(!await identity.validate(signature,concat(domain,raw)))throw Error('轨迹目录回执签名验证失败');
 return Object.freeze({signed:[raw,signature],objectId:version.objectId,revisionHash:version.revisionHash,revision:record[4],state:record[5],
   directoryPublicKey:key,directoryEpoch:epoch,requestId:request,retentionPolicy:1,acceptedAt:record[12],directoryConfirmed:true});
}
