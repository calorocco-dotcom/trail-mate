export const isGroupHash=key=>/^rt\.group\.\d+\.(destination_hash|identity_hash)$/.test(key);
export function decodeConfigurationBinary(key,input){
 if(typeof input!=='string')throw Error('Binary input must be text');
 const value=input.trim(),hex=value.startsWith('0x')?value.slice(2):value;
 if(isGroupHash(key)&&/^[0-9a-fA-F]{32}$/.test(hex))return Uint8Array.from(hex.match(/../g),pair=>parseInt(pair,16));
 return Uint8Array.from(atob(value),character=>character.charCodeAt(0));
}
