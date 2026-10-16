import {bytes,equal,integer} from './protocol.js';
const hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');
const stages=new Set(['prepared','submitting','admitted','stopping','confirmed','stopped','failed','unknown']);
const key=author=>'trailmate.geo.publication.v1.'+hex(author);
const parseHex=(value,size)=>{if(typeof value!=='string'||!new RegExp(`^[0-9a-f]{${size*2}}$`).test(value))throw Error('浏览器发布记录损坏，请保留记录并查询设备任务');
  return Uint8Array.from(value.match(/../g),part=>parseInt(part,16));};
const generation=value=>{if(typeof value!=='string'||!/^([1-9][0-9]*)$/.test(value))throw Error('浏览器发布草稿代次无效');
  const result=BigInt(value);if(result>0xffffffffffffffffn)throw Error('浏览器发布草稿代次无效');return result;};

// Correlation metadata only. No private key, configuration, or draft body.
export function savePublicationCheckpoint(record,storage=localStorage){
  const p=record?.prepared;
  if(!p||!stages.has(record.stage)||!bytes(record.owner,16)||!record.owner.some(b=>b)||!bytes(p.authorKey,64)||!bytes(p.prepareId,16)||!p.prepareId.some(b=>b)||!bytes(p.id,16)||
     !bytes(p.directory,16)||!bytes(p.directoryEpoch,16)||!bytes(p.contentHash,32)||!integer(p.authorGeneration,1,0xffffffff)||
     !integer(p.fromRevision,0,0xffffffff)||!integer(p.toRevision,1,0xffffffff)||
     !(p.toRevision===p.fromRevision||(p.fromRevision!==0xffffffff&&p.toRevision===p.fromRevision+1)))throw Error('浏览器发布记录绑定无效');
  generation(String(p.generation));
  if(typeof p.generation==='number'&&!Number.isSafeInteger(p.generation))throw Error('浏览器发布草稿代次无效');
  const title=typeof record.title==='string'?record.title:'';
  if(new TextEncoder().encode(title).length>96)throw Error('浏览器发布标题无效');
  const value={version:1,stage:record.stage,title,owner:hex(record.owner),prepared:{prepareId:hex(p.prepareId),id:hex(p.id),generation:String(p.generation),
    authorKey:hex(p.authorKey),authorGeneration:p.authorGeneration,directory:hex(p.directory),directoryEpoch:hex(p.directoryEpoch),
    contentHash:hex(p.contentHash),fromRevision:p.fromRevision,toRevision:p.toRevision}};
  try{storage.setItem(key(p.authorKey),JSON.stringify(value));}
  catch(error){throw Error('无法更新浏览器发布记录，请检查存储并保留操作标识；不会创建替代操作',{cause:error});}
}

export function loadPublicationCheckpoint(author,storage=localStorage){
  if(!bytes(author,64))throw Error('浏览器发布作者无效');
  const raw=storage.getItem(key(author));if(raw===null)return undefined;
  if(raw.length>2048)throw Error('浏览器发布记录超过限制');
  let value;try{value=JSON.parse(raw);}catch{throw Error('浏览器发布记录损坏，请保留记录并查询设备任务');}
  if(!value||Array.isArray(value)||value.version!==1||!stages.has(value.stage)||typeof value.title!=='string')throw Error('浏览器发布记录格式无效');
  const p=value.prepared;
  if(!p)throw Error('浏览器发布准备记录无效');
  const prepared={prepareId:parseHex(p.prepareId,16),id:parseHex(p.id,16),generation:generation(p.generation),authorKey:parseHex(p.authorKey,64),
    authorGeneration:p.authorGeneration,directory:parseHex(p.directory,16),directoryEpoch:parseHex(p.directoryEpoch,16),contentHash:parseHex(p.contentHash,32),
    fromRevision:p.fromRevision,toRevision:p.toRevision,expiresAt:0};
  if(!equal(prepared.authorKey,author))throw Error('浏览器发布记录作者不一致');
  // Reuse the whitelist validation without changing persistent state.
  const record={stage:value.stage,title:value.title,owner:parseHex(value.owner,16),prepared,restored:true};
  savePublicationCheckpoint(record,{setItem(){}});
  return record;
}

export function managementOwner(storage=localStorage,random=()=>crypto.getRandomValues(new Uint8Array(16))){
  const ownerKey='trailmate.geo.usb-owner.v1',saved=storage.getItem(ownerKey);
  if(saved!==null){const id=parseHex(saved,16);if(!id.some(b=>b))throw Error('设备维护会话标识损坏');return id;}
  const id=random();if(!bytes(id,16)||!id.some(b=>b))throw Error('设备维护会话标识无效');
  storage.setItem(ownerKey,hex(id));return id;
}
