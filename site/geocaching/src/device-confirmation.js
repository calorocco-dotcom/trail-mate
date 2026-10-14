import {OPERATION} from '../../device/src/management-protocol.js';
import {readCurrentDeviceAuthor} from './current-device-author.js';
import {uploadObject} from '../../device/src/object-transfer.js';
import {bytes,equal,encode,verifySigned} from './protocol.js';
import {verifyPublicationReceipt} from './publication-receipt.js';

export async function uploadDevicePublicationConfirmation(session,signed,receipt,author,{id,operations=[],signal,onProgress}={}){
 if(![32,33,35,OPERATION.CACHE_CONFIRM_BEGIN,OPERATION.CACHE_CONFIRM_STATUS].every(op=>operations.includes(op)))throw Error('Device confirmation capability unavailable');
 if(!bytes(id,16)||!id.some(value=>value)||!Array.isArray(signed)||signed.length!==2||!(signed[0] instanceof Uint8Array)||!bytes(signed[1],64)||
    !receipt?.raw||receipt.raw.length>192||!author?.verified||!bytes(author.publicKey,64))throw Error('Invalid device confirmation input');
 const content=signed.map(value=>value.slice()),transferId=id.slice(),expected={...author,publicKey:author.publicKey.slice()};
 const proof=Object.fromEntries(['raw','signature','publicKey','delivery','epoch'].map(key=>[key,receipt[key]?.slice()]));
 if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');
 const verified=await verifySigned(content);
 if(!equal(verified.record[1],expected.publicKey))throw Error('Confirmation belongs to another author');
 await verifyPublicationReceipt([proof.raw,proof.signature],content,proof);
 const source=encode(content);if(source.length>4166)throw Error('Confirmation upload exceeds device budget');
 const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',source));
 await readCurrentDeviceAuthor(session,expected,{signal});
 await uploadObject(session,source,{id:transferId,kind:6,sha256:hash,signal,onProgress});
 return {id:transferId,sha256:hash,bytes:source.length,sealed:true,authorGeneration:expected.generation,receipt:proof};
}
