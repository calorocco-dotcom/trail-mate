import {equal,decode} from '../../geocaching/src/protocol.js';
import {encodeTrackIPCheckpoint,decodeTrackIPCheckpoint} from './ip-publication-journal.js';
import {verifyTrackManifest} from './manifest.js';
import {verifyTrackReceipt} from './receipt.js';
import {trackShareLink} from './shared-id.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');

export async function restoreTrackIPHistoryFile(record,network,{baseUrl=globalThis.location?.href,signal,onProgress}={}){
 const frozen=decodeTrackIPCheckpoint(encodeTrackIPCheckpoint(record));
 const check=()=>{if(signal?.aborted)throw signal.reason??new DOMException('Paused','AbortError');};check();
 if(!frozen.signature||!network||typeof network.readTrack!=='function')throw Error('Signed track history and browser network required');
 const signed=[frozen.plan.manifest,frozen.signature],verified=await verifyTrackManifest(signed,{expectedAuthor:frozen.authorKey});check();
 if(frozen.stage==='confirmed')await verifyTrackReceipt(decode(frozen.receipt,1200),{directoryPublicKey:frozen.directoryKey,directoryEpoch:frozen.epoch,requestId:frozen.requestId,manifestSigned:signed});
 const key=hex(frozen.delivery),entry=network.directories.get(key);
 if(!entry?.ready||!equal(entry.epoch,frozen.epoch)||!equal(await entry.identity.getPublicKey(),frozen.directoryKey))throw Error('Historical track directory changed');check();
 const reference=trackShareLink(baseUrl,verified.objectId,{revision:verified.revisionHash});
 const result=await network.readTrack(key,reference,{signal,onProgress,download:true});check();
 if(!result||!equal(result.manifest,frozen.plan.manifest)||!equal(result.signature,frozen.signature)||!(result.source instanceof Uint8Array)||result.source.length!==frozen.plan.fileBytes)throw Error('Historical track response differs');
 const source=result.source.slice();
 if(!equal(new Uint8Array(await crypto.subtle.digest('SHA-256',source)),frozen.plan.fileHash))throw Error('Historical KML hash differs');check();
 return {source,name:verified.record[9],objectId:verified.objectId,revisionHash:verified.revisionHash,revision:verified.record[4]};
}
