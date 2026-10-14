// A domain-separated hash of typed records, not of the wire document. This
// matches configurationChanges: order and equivalent float32/text encodings
// do not imply a setting change; unknown records retain their exact raw value.
export async function configurationContentHash(document,digest=async bytes=>new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))){
 const float=new DataView(new ArrayBuffer(4));
 const records=document.entries().sort((left,right)=>left.key<right.key?-1:left.key>right.key?1:0).map(record=>{
  let value=record.value;
  if(value===undefined)value=record.raw;
  else if(value instanceof Uint8Array)value=Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
  else if(record.type==='f32'){float.setFloat32(0,value===0?0:value,false);value=float.getUint32(0,false).toString(16).padStart(8,'0');}
  return [record.key,record.type,value];
 });
 const hash=await digest(new TextEncoder().encode(JSON.stringify(['trailmate.config.typed.v1',records])));
 if(!(hash instanceof Uint8Array)||hash.length!==32)throw Error('CONFIG_CONTENT_HASH_INVALID');
 return Array.from(hash,byte=>byte.toString(16).padStart(2,'0')).join('');
}
