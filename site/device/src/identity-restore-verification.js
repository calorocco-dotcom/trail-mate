import {proveDeviceAuthor} from './author-identity.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const fail=code=>{throw Object.assign(Error(code),{code});};
// Expectations contain public metadata only and can survive USB disconnection.
// A commit response alone never reports a restored, operational identity.
export async function verifyRestoredDeviceIdentity(session,expectation,{signal}={}){
 if(!expectation||!/^[0-9a-f]{128}$/.test(expectation.publicKey)||!/^[0-9a-f]{32}$/.test(expectation.identityHash)||!/^[0-9a-f]{32}$/.test(expectation.delivery)||!Number.isInteger(expectation.generation)||expectation.generation<1||expectation.generation>0xffffffff)fail('IDENTITY_RESTORE_EXPECTATION_INVALID');
 const author=await proveDeviceAuthor(session,{signal});
 if(hex(author.publicKey)!==expectation.publicKey||hex(author.identityHash)!==expectation.identityHash||hex(author.destination)!==expectation.delivery||author.generation!==expectation.generation)fail('IDENTITY_RESTORE_NOT_CONFIRMED');
 return Object.freeze({state:'verified',author});
}
