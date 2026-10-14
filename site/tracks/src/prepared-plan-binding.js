import {decode,bytes,equal} from '../../geocaching/src/protocol.js';
import {validateTrackPublicationResume} from './publication-plan.js';
const utf8=new TextEncoder();
async function hash(domain,...parts){const prefix=utf8.encode(domain+'\0'),raw=new Uint8Array(prefix.length+parts.reduce((n,p)=>n+p.length,0));raw.set(prefix);let offset=prefix.length;for(const part of parts){raw.set(part,offset);offset+=part.length;}return new Uint8Array(await crypto.subtle.digest('SHA-256',raw));}
// Validate before retaining a job binding or sending TrackCommit. Both hashes
// are domain-separated exactly as the device's signed revision preparation.
export async function validatePreparedPlanBinding(plan,prepared,author,fileHash,fileBytes){
 const retained=validateTrackPublicationResume(plan,author,fileHash,fileBytes),record=decode(retained.manifest,4096);
 if(prepared?.prepared!==true||!bytes(prepared.operationId,16)||!equal(prepared.operationId,retained.operationId)||prepared.generation!==retained.generation||prepared.revision!==record[4])throw Error('准备结果与持久轨迹计划不一致');
 const objectId=await hash('TrailMate.Track.ID.v1',record[1],record[2]),revisionHash=await hash('TrailMate.Track.Revision.v1',retained.manifest);
 if(!bytes(prepared.objectId,32)||!equal(prepared.objectId,objectId)||!bytes(prepared.revisionHash,32)||!equal(prepared.revisionHash,revisionHash))throw Error('准备结果与持久轨迹清单不一致');
 return retained;
}
