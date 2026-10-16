import {Identity} from '@reticulum/core';
import {bytes,decode,encode,equal,integer,verifySigned} from './protocol.js';
const utf8=new TextEncoder();
const join=(...parts)=>Uint8Array.from(parts.flatMap(part=>Array.from(part)));

// A directory proof can cross USB without retaining an LXMF message or its
// transport buffers. It confirms the named head as observed by this directory.
export async function verifyPublicationReceipt(proof,signed,directory){
 if(!Array.isArray(proof)||proof.length!==2||!(proof[0] instanceof Uint8Array)||!proof[0].length||proof[0].length>256||!bytes(proof[1],64)||
    !bytes(directory?.publicKey,64)||!bytes(directory?.delivery,16)||!bytes(directory?.epoch,16))throw Error('Invalid directory receipt proof');
 const raw=proof[0].slice(),signature=proof[1].slice(),publicKey=directory.publicKey.slice(),delivery=directory.delivery.slice(),epoch=directory.epoch.slice();
 const content=signed.map(part=>part.slice()),value=decode(raw,256);
 if(!Array.isArray(value)||value.length!==8||value[0]!==1||!equal(encode(value),raw)||!equal(value[1],delivery)||!equal(value[2],epoch)||
    !bytes(value[3],32)||!integer(value[4],1,0xffffffff)||!bytes(value[5],32)||!integer(value[6],0,2)||!integer(value[7],1,253402300799))throw Error('Directory receipt fields differ');
 const identity=await Identity.fromPublicKey(publicKey);
 const name=(await Identity.fullHash(utf8.encode('lxmf.delivery'))).slice(0,10);
 if(!equal(await Identity.truncatedHash(join(name,identity.identityHash)),delivery)||
    !await identity.validate(signature,join(utf8.encode('trailmate.geocache/receipt/v1\0'),raw)))throw Error('Directory receipt signature invalid');
 const verified=await verifySigned(content);
 if(!equal(value[3],verified.summary[0])||value[4]!==verified.summary[1]||!equal(value[5],verified.summary[2])||value[6]!==verified.summary[3])throw Error('Directory receipt confirms different content');
 return {proof:[raw,signature],publicKey,delivery,epoch,observedAt:value[7],cacheId:verified.cacheId,revision:value[4],revisionHash:verified.revisionHash,state:value[6]};
}
