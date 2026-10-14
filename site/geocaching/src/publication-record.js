import {Identity} from '@reticulum/core';
import {bytes,equal,integer,encode,verifySigned} from './protocol.js';
import {validateDraftPublication} from './device-draft.js';
const utf8=new TextEncoder(),join=(...parts)=>Uint8Array.from(parts.flatMap(part=>Array.from(part)));
export async function deviceCacheId(authorPublicKey,nonce){
 if(!bytes(authorPublicKey,64)||!bytes(nonce,16))throw Error('Invalid cache identity');
 return Identity.fullHash(join(utf8.encode('trailmate.geocache/id/v1\0'),authorPublicKey,nonce));
}
export async function buildPublicationRecord(draft,author,current,{nowSeconds=Math.floor(Date.now()/1000)}={}){
 if(!author?.verified||!bytes(author.publicKey,64)||!bytes(draft?.id,16)||!integer(nowSeconds,1,253402300799)||
    !current||typeof current.exists!=='boolean')throw Error('Invalid publication context');
 const publicKey=author.publicKey.slice(),nonce=draft.id.slice(),fields=validateDraftPublication(draft),base=draft.baseHash?.slice()??null;
 const exists=current.exists,parentSigned=exists?[current.signed?.[0]?.slice(),current.signed?.[1]?.slice()]:null;
 if(draft.authorKey!==null&&draft.authorKey!==undefined&&!equal(draft.authorKey,publicKey))throw Error('Draft belongs to another author');
 const id=await deviceCacheId(publicKey,nonce);
 let revision=1,previous=null,created=nowSeconds,updated=nowSeconds;
 if(exists){
  const parent=await verifySigned(parentSigned),r=parent.record;
  if(!equal(parent.summary[0],id)||!equal(r[1],publicKey)||!equal(r[2],nonce))throw Error('Current version belongs to another cache');
  if(base&&!equal(base,parent.summary[2]))throw Error('Draft base version is stale');
  if(r[5]===2)throw Error('Archived cache cannot be revised');
  if(r[3]===0xffffffff)throw Error('Cache revision exhausted');
  revision=r[3]+1;previous=parent.summary[2].slice();created=r[14];updated=Math.max(nowSeconds,r[15]);
 }else if(base||fields.state===2)throw Error('Existing version is required for this publication');
 const raw=encode([1,publicKey,nonce,revision,previous,fields.state,fields.latitudeE7,fields.longitudeE7,fields.name,fields.description,fields.hint,
                   fields.difficultyX2,fields.terrainX2,fields.containerSize,created,updated]);
 if(raw.length>4096)throw Error('Publication exceeds device record limit');
 return {raw,cacheId:id,revision,previousHash:previous,state:fields.state,authorKey:publicKey,createdAt:created,updatedAt:updated};
}
