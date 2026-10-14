const PREFIX='tmtrk1_';
const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
function binary(value){if(!/^[0-9a-fA-F]{64}$/.test(value))throw Error('轨迹标识必须包含完整的 32 字节 ID');return Uint8Array.from(value.match(/../g),b=>parseInt(b,16));}
export function trackId(bytes){if(!(bytes instanceof Uint8Array)||bytes.length!==32)throw Error('Invalid track object ID');return PREFIX+hex(bytes);}
export function parseTrackReference(input,{baseUrl}={}){
 if(typeof input!=='string')throw Error('请输入轨迹 ID 或分享链接');
 const text=input.trim();
 if(text.startsWith(PREFIX))return {id:binary(text.slice(PREFIX.length)),revision:null};
 let url;try{url=new URL(text,baseUrl);}catch{throw Error('无效的轨迹 ID 或分享链接');}
 if(!['https:','http:'].includes(url.protocol)||url.searchParams.getAll('id').length!==1||url.searchParams.getAll('rev').length>1)throw Error('无效的轨迹分享链接');
 const id=url.searchParams.get('id');if(!id.startsWith(PREFIX))throw Error('此链接不是 Trail Mate 轨迹分享');
 return {id:binary(id.slice(PREFIX.length)),revision:url.searchParams.has('rev')?binary(url.searchParams.get('rev')):null};
}
export function trackShareLink(baseUrl,id,{revision}={}){
 const base=new URL(baseUrl);if(!['http:','https:'].includes(base.protocol))throw Error('Invalid site base URL');
 // baseUrl is the deployment root, including GitHub Pages repository prefix.
 base.search='';base.hash='';if(!base.pathname.endsWith('/'))base.pathname+='/';
 const link=new URL('tracks/',base);link.searchParams.set('id',trackId(id));
 if(revision!==undefined){if(!(revision instanceof Uint8Array)||revision.length!==32)throw Error('Invalid track revision');link.searchParams.set('rev',hex(revision));}
 return link.href;
}
export async function copyTrackReference(text,{clipboard=globalThis.navigator?.clipboard}={}){
 if(typeof text!=='string'||!text)throw Error('Nothing to copy');
 try{if(!clipboard?.writeText)throw Error('Clipboard unavailable');await clipboard.writeText(text);return {copied:true,text};}
 catch{return {copied:false,text};} // UI must present selectable full text, never report success.
}
