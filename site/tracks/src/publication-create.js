import {bytes,integer} from '../../geocaching/src/protocol.js';
import {parseKml} from './kml.js';
import {manifestGeometryBounds} from './manifest-geometry.js';
import {summarizeKml} from './kml-summary.js';
import {encodeTrackManifest} from './manifest.js';
import {encodeTrackPublicationPlan,decodeTrackPublicationPlan} from './publication-plan.js';

// Only creates a first active revision. Updates must retain the original nonce,
// creation time and verified predecessor, rather than regenerate an identity.
export async function createTrackPublicationPlan(kml,author,{title,description='',kind=0,
  assets=null,metadata,createdAt=Math.floor(Date.now()/1000),randomBytes=size=>crypto.getRandomValues(new Uint8Array(size))}={}){
  if(!(kml instanceof Uint8Array)||!kml.length||kml.length>8388608||!author?.verified||!bytes(author.publicKey,64)||
     !integer(author.generation,1,0xffffffff)||!integer(kind,0,1)||typeof randomBytes!=='function')throw Error('轨迹准备需要已验证的设备身份与 KML');
  const content=kml.slice(),publicKey=author.publicKey.slice(),generation=author.generation;
  const model=parseKml(content),geometry=manifestGeometryBounds(model),summary=summarizeKml(model);
  const fileHash=new Uint8Array(await crypto.subtle.digest('SHA-256',content));
  const nonce=randomBytes(16).slice(),operationId=randomBytes(16).slice(),kmlUploadId=randomBytes(16).slice(),manifestUploadId=randomBytes(16).slice();
  const distance=Math.round(summary.distanceM??0),ascent=summary.ascentM===null?null:Math.round(summary.ascentM),
    duration=summary.durationS===null?null:Math.round(summary.durationS);
  const record=[1,publicKey,nonce,kind,1,null,0,createdAt,createdAt,title,description,
    geometry.bbox,[distance,duration,ascent,geometry.pointCount],
    [fileHash,content.length,'application/vnd.google-earth.kml+xml',1]];
  if(assets!==null||metadata!==undefined){record[0]=2;record.push(metadata??[0,[],0,summary.segments,0,0,0,null],assets);}
  const manifest=encodeTrackManifest(record);
  return decodeTrackPublicationPlan(encodeTrackPublicationPlan({operationId,kmlUploadId,manifestUploadId,generation,
    manifest,fileHash,fileBytes:content.length}));
}
