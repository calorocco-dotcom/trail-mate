import {bytes} from '../../geocaching/src/protocol.js';
import {OPERATION} from '../../device/src/management-protocol.js';
import {prepareTrack} from './device-prepare.js';
export async function prepareTrackPackage(session,bindings,packageUploadId,options={}){
 if(packageUploadId!==null&&(!bytes(packageUploadId,16)||!packageUploadId.some(Boolean)))throw Error('Invalid device package upload');
 const packageId=packageUploadId?.slice()??null;let signature;
 const mapped={request:async(op,body)=>{
  if(op!==OPERATION.TRACK_PREPARE)throw Error('Unexpected device package operation');
  const reply=await session.request(OPERATION.TRACK_PACKAGE_SIGN_PREPARE,[...body,packageId?.slice()??null]);
  if(!Array.isArray(reply)||reply.length!==6||(reply[1]===1?!bytes(reply[5],64):reply[5]!==null))throw Error('Invalid device package signature response');
  if(reply[1]===1)signature=reply[5].slice();return reply.slice(0,5);
 }};
 const prepared=await prepareTrack(mapped,bindings,options);
 if(!signature)throw Error('Device did not return package signature');
 return {...prepared,signature};
}
