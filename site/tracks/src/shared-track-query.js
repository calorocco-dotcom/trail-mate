import {Identity} from '@reticulum/core';
import {bytes,integer,equal,encode,validText,viewportBoxes} from '../../geocaching/src/protocol.js';
import {validateTrackEditorMetadata} from './track-editor-metadata.js';

const fail=code=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
const snapshotValue=value=>typeof value==='bigint'?value>=0n&&value<=0x7fffffffffffffffn:integer(value,0,Number.MAX_SAFE_INTEGER);
const boxValid=box=>Array.isArray(box)&&box.length===4&&integer(box[0],-1800000000,1799999999)&&integer(box[2],-1800000000,1799999999)&&integer(box[1],-900000000,900000000)&&integer(box[3],box[1],900000000);
const compare=(a,b)=>{for(let i=0;i<32;i++)if(a[i]!==b[i])return a[i]-b[i];return 0;};
const arcs=box=>box[0]<=box[2]?[[box[0],box[2]]]:[[box[0],1799999999],[-1800000000,box[2]]];
function intersects(a,b){return a[1]<=b[3]&&b[1]<=a[3]&&arcs(a).some(([west,east])=>arcs(b).some(([otherWest,otherEast])=>west<=otherEast&&otherWest<=east));}

// Leaflet bounds use south/west/north/east and may span wrapped longitudes.
// Directory boxes use west/south/east/north, with canonical E7 longitudes.
export function sharedTrackViewportBoxes(bounds){
 let boxes;try{boxes=viewportBoxes(bounds);}catch{fail('TRACK_QUERY_BOUNDS_INVALID');}
 const result=boxes.map(([south,west,north,east])=>[Math.min(west,1799999999),south,Math.min(east,1799999999),north]);
 if(boxes.some(box=>box[3]===1800000000)&&!result.some(box=>box[0]===-1800000000))result.push([-1800000000,result[0][1],-1800000000,result[0][3]]);
 return result;
}

export function validateSharedTrackSummary(row,{metadata=false}={}){
 if(Array.isArray(row)&&row[2]===1&&row[4]!==0)fail('TRACK_QUERY_SUMMARY_INVALID');
 if(!Array.isArray(row)||row.length!==(metadata?12:10)||!bytes(row[0],32)||!bytes(row[1],32)||!integer(row[2],1,0xffffffff)||!integer(row[3],0,1)||!integer(row[4],0,1)||!validText(row[5],128,false,true)||!boxValid(row[6])||!Array.isArray(row[7])||row[7].length!==4||!integer(row[7][0],0,0xffffffff)||!row[7].slice(1,3).every(value=>value===null||integer(value,0,0xffffffff))||!integer(row[7][3],1,100000)||!integer(row[8],1,8388608)||!bytes(row[9],64)||!row[9].some(Boolean))fail('TRACK_QUERY_SUMMARY_INVALID');
 let extra={};if(metadata){try{const value=validateTrackEditorMetadata({activity:row[10],tags:row[11]});extra={activity:value.activity,tags:Object.freeze(value.tags)};}catch{fail('TRACK_QUERY_SUMMARY_INVALID');}}
 return Object.freeze({objectId:row[0].slice(),revisionHash:row[1].slice(),revision:row[2],kind:row[3],state:row[4],title:row[5],bounds:row[6].slice(),statistics:row[7].slice(),fileBytes:row[8],author:row[9].slice(),authorVerified:false,...extra});
}

export async function querySharedTrackPage(request,{box,kind=null,author=null,state=0,activity=null,tags=[],limit=16,snapshot=null,cursor=null,signal}={}){
 check(signal);
 if(typeof request!=='function'||!boxValid(box)||kind!==null&&!integer(kind,0,1)||state!==null&&!integer(state,0,1)||author!==null&&!bytes(author,64)||!integer(limit,1,32)||snapshot!==null&&!snapshotValue(snapshot)||cursor!==null&&(!Array.isArray(cursor)||cursor.length!==2||!bytes(cursor[0],32)||!bytes(cursor[1],32)||snapshot===null))fail('TRACK_QUERY_OPTIONS_INVALID');
 let labels;try{labels=validateTrackEditorMetadata({activity:activity??0,tags});if(activity!==null&&!integer(activity,0,255))fail('TRACK_QUERY_OPTIONS_INVALID');}catch{fail('TRACK_QUERY_OPTIONS_INVALID');}
 const metadata=activity!==null||labels.tags.length>0;
 const bounds=box.slice(),owner=author?.slice()??null,previous=cursor?.map(value=>value.slice())??null;
 const binding=await Identity.fullHash(encode(metadata?['trailmate.track.query.metadata.v1',bounds,kind,owner,state,activity,labels.tags]:[bounds,kind,owner,state]));check(signal);
 if(previous&&!equal(previous[1],binding))fail('TRACK_QUERY_CURSOR_MISMATCH');
 const response=await request(metadata?15:1,metadata?[bounds.slice(),kind,owner?.slice()??null,state,activity,labels.tags,limit,snapshot,previous?.map(value=>value.slice())??null]:[bounds.slice(),kind,owner?.slice()??null,state,limit,snapshot,previous?.map(value=>value.slice())??null],{signal});check(signal);
 if(!Array.isArray(response)||response.length!==3||!snapshotValue(response[0])||!Array.isArray(response[2])||response[2].length>limit)fail('TRACK_QUERY_RESPONSE_INVALID');
 if(snapshot!==null&&BigInt(response[0])!==BigInt(snapshot))fail('TRACK_QUERY_SNAPSHOT_CHANGED');
 const items=response[2].map(row=>validateSharedTrackSummary(row,{metadata}));let after=previous?.[0];
 for(const item of items){if(after&&compare(item.objectId,after)<=0||kind!==null&&item.kind!==kind||state!==null&&item.state!==state||owner&&!equal(owner,item.author)||activity!==null&&item.activity!==activity||metadata&&labels.tags.some(tag=>!item.tags.includes(tag))||!intersects(bounds,item.bounds))fail('TRACK_QUERY_FILTER_MISMATCH');after=item.objectId;}
 const next=response[1];
 if(next!==null&&(!Array.isArray(next)||next.length!==2||!bytes(next[0],32)||!bytes(next[1],32)||!equal(next[1],binding)||previous&&compare(next[0],previous[0])<=0||after&&compare(next[0],after)<0))fail('TRACK_QUERY_CURSOR_MISMATCH');
 return {snapshot:response[0],cursor:next?.map(value=>value.slice())??null,items};
}
