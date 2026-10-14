const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true});
export const AUTO_REPLY_MAX_BYTES=171;
export const isConfigurationTextBlob=key=>key==='chat.auto_reply.text';
const invalid=()=>{throw Object.assign(Error('Invalid UTF-8 text'),{code:'invalid',params:{}});};
export function configurationTextBlobValue(record){
 if(!isConfigurationTextBlob(record.key)||record.type!=='b64'||!(record.value instanceof Uint8Array))return undefined;
 try{const value=decoder.decode(record.value);return value.includes('\0')?undefined:value;}catch{return undefined;}
}
export function validateConfigurationTextBlob(value){
 if(!(value instanceof Uint8Array))invalid();
 if(value.length>AUTO_REPLY_MAX_BYTES)throw Object.assign(Error('Text exceeds device capacity'),{code:'CONFIG_UTF8_LIMIT',params:{max:AUTO_REPLY_MAX_BYTES}});
 let text;try{text=decoder.decode(value);}catch{invalid();}
 if(text.includes('\0'))invalid();
}
export function encodeConfigurationTextBlob(text){
 if(typeof text!=='string'||text.includes('\0'))invalid();
 const value=encoder.encode(text);
 if(decoder.decode(value)!==text)invalid();
 validateConfigurationTextBlob(value);return value;
}
