import {Identity} from '@reticulum/core';
import {bytes,equal,integer,encode,validText} from '../../geocaching/src/protocol.js';
import {validateSharedTrackSummary} from './shared-track-query.js';
import {validateTrackEditorMetadata} from './track-editor-metadata.js';
const fail=code=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const snapshotValid=value=>typeof value==='bigint'?value>=0n&&value<=0x7fffffffffffffffn:integer(value,0,Number.MAX_SAFE_INTEGER);
const compare=(a,b)=>{for(let i=0;i<32;i++)if(a[i]!==b[i])return a[i]-b[i];return 0;};
const boxValid=value=>Array.isArray(value)&&value.length===4&&integer(value[0],-1800000000,1799999999)&&integer(value[2],-1800000000,1799999999)&&integer(value[1],-900000000,900000000)&&integer(value[3],value[1],900000000);
const arcs=box=>box[0]<=box[2]?[[box[0],box[2]]]:[[box[0],1799999999],[-1800000000,box[2]]];
const intersects=(a,b)=>a[1]<=b[3]&&b[1]<=a[3]&&arcs(a).some(([w,e])=>arcs(b).some(([ow,oe])=>w<=oe&&ow<=e));

// request must already authenticate the selected directory and correlate its
// envelope. These checks bind the extension's hierarchy, boundary and query.
export async function createTrackRegionClient(request,{signal,metadataSupported=false}={}){
 check(signal);if(typeof request!=='function')fail('TRACK_REGION_OPTIONS_INVALID');
 let caps;try{caps=await request(9,[],{signal});}catch(error){check(signal);if(error.status===422&&error.diagnostic==='region capability unsupported')fail('TRACK_REGION_UNSUPPORTED');throw error;}
 check(signal);
 if(!Array.isArray(caps)||caps.length!==8||caps[0]!==1||caps[1]!=='trailmate.track.regions.v1'||typeof caps[2]!=='string'||! /^[a-z][a-z0-9.-]{0,23}$/.test(caps[2])||!bytes(caps[3],32)||!caps[3].some(Boolean)||
    !integer(caps[4],0,8)||!integer(caps[5],1,32)||!integer(caps[6],1,1024)||!Array.isArray(caps[7])||![9,10,11,12,13].every(op=>caps[7].includes(op)))fail('TRACK_REGION_CAPABILITIES_INVALID');
 const namespace=caps[2],version=caps[3].slice(),depth=caps[4],pageLimit=caps[5],chunkBytes=caps[6];
 const identifier=value=>typeof value==='string'&&value.startsWith(namespace+':')&&/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(value.slice(namespace.length+1));
 function node(row){
  if(!Array.isArray(row)||row.length!==7||!identifier(row[0])||(row[1]!==null&&!identifier(row[1]))||row[0]===row[1]||!integer(row[2],0,depth)||((row[1]===null)!==(row[2]===0))||
     !validText(row[3],128,false,true)||!Array.isArray(row[4])||row[4].length>32||!boxValid(row[5])||!integer(row[6],0,10000)||(row[2]===depth&&row[6]))fail('TRACK_REGION_NODE_INVALID');
  const names={};
  for(const pair of row[4]){if(!Array.isArray(pair)||pair.length!==2||typeof pair[0]!=='string'||! /^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8}){0,2}$/.test(pair[0])||Object.hasOwn(names,pair[0])||!validText(pair[1],128,false,true))fail('TRACK_REGION_NODE_INVALID');names[pair[0]]=pair[1];}
  return Object.freeze({id:row[0],parent:row[1],level:row[2],name:row[3],names:Object.freeze(names),bounds:Object.freeze(row[5].slice()),childCount:row[6],version:version.slice()});
 }
 const validNode=value=>value&&identifier(value.id)&&bytes(value.version,32)&&equal(value.version,version)&&integer(value.level,0,depth)&&boxValid(value.bounds);
 async function children(parent=null,{after=null,limit=Math.min(16,pageLimit),signal}={}){
  check(signal);if(parent!==null&&!validNode(parent)||after!==null&&!identifier(after)||!integer(limit,1,pageLimit))fail('TRACK_REGION_PAGE_OPTIONS_INVALID');
  const parentId=parent?.id??null,level=parent?parent.level+1:0;
  const response=await request(10,[version.slice(),parentId,after,limit],{signal});check(signal);
  if(!Array.isArray(response)||response.length!==3||!equal(response[0],version)||!Array.isArray(response[2])||response[2].length>limit)fail('TRACK_REGION_PAGE_INVALID');
  const items=response[2].map(node);let previous=after;
  for(const item of items){if(item.parent!==parentId||item.level!==level||previous!==null&&item.id<=previous)fail('TRACK_REGION_PAGE_INVALID');previous=item.id;}
  const cursor=response[1];if(cursor!==null&&(!identifier(cursor)||!items.length||cursor!==items.at(-1).id))fail('TRACK_REGION_PAGE_INVALID');
  return {version:version.slice(),cursor,items};
 }
 async function region(id,{signal,maxBytes=8388608}={}){
  check(signal);if(!identifier(id)||!integer(maxBytes,1,8388608))fail('TRACK_REGION_OPTIONS_INVALID');
  const response=await request(11,[id,version.slice()],{signal});check(signal);
  if(!Array.isArray(response)||response.length!==3||!equal(response[0],version)||!Array.isArray(response[2])||response[2].length!==3||response[2][0]!=='region-e7-json-v1'||!bytes(response[2][1],32)||!integer(response[2][2],1,maxBytes))fail('TRACK_REGION_DESCRIPTOR_INVALID');
  const selected=node(response[1]);if(selected.id!==id)fail('TRACK_REGION_DESCRIPTOR_INVALID');
  const digest=response[2][1].slice(),total=response[2][2],raw=new Uint8Array(total);
  for(let offset=0;offset<total;){const length=Math.min(chunkBytes,total-offset),chunk=await request(12,[id,version.slice(),offset,length],{signal});check(signal);
   if(!Array.isArray(chunk)||chunk.length!==4||!equal(chunk[0],digest)||chunk[1]!==total||chunk[2]!==offset||!bytes(chunk[3],length))fail('TRACK_REGION_BOUNDARY_CHUNK_INVALID');raw.set(chunk[3],offset);offset+=length;
  }
  if(!equal(await Identity.fullHash(raw),digest))fail('TRACK_REGION_BOUNDARY_HASH_MISMATCH');check(signal);
  let boundary;try{boundary=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(raw));}catch{fail('TRACK_REGION_BOUNDARY_INVALID');}
  if(!Array.isArray(boundary)||boundary.length!==4||boundary[0]!==1||boundary[1]!==hex(version)||boundary[2]!==id||!Array.isArray(boundary[3])||!boundary[3].length||boundary[3].length>1024)fail('TRACK_REGION_BOUNDARY_INVALID');
  let points=0,south=900000000,north=-900000000;const xs=new Set(),wrap=x=>((x+1800000000)%3600000000+3600000000)%3600000000-1800000000;
  const polygons=boundary[3].map(polygon=>{
   if(!Array.isArray(polygon)||!polygon.length||polygon.length>1024)fail('TRACK_REGION_BOUNDARY_INVALID');
   return polygon.map((ring,index)=>{
    if(!Array.isArray(ring)||ring.length<4||ring.length>100000)fail('TRACK_REGION_BOUNDARY_INVALID');let area=0n,west=3600000000,east=-3600000000;
    for(let i=0;i<ring.length;i++){const point=ring[i];if(!Array.isArray(point)||point.length!==2||!integer(point[0],-3600000000,3600000000)||!integer(point[1],-900000000,900000000)||++points>200000)fail('TRACK_REGION_BOUNDARY_INVALID');
     west=Math.min(west,point[0]);east=Math.max(east,point[0]);if(!index){xs.add(wrap(point[0]));south=Math.min(south,point[1]);north=Math.max(north,point[1]);}
     if(i){const previous=ring[i-1];if(Math.abs(previous[0]-point[0])>=1800000000)fail('TRACK_REGION_BOUNDARY_INVALID');area+=BigInt(previous[0])*BigInt(point[1])-BigInt(point[0])*BigInt(previous[1]);}
    }
    if(ring[0][0]!==ring.at(-1)[0]||ring[0][1]!==ring.at(-1)[1]||area===0n||east-west>=1800000000)fail('TRACK_REGION_BOUNDARY_INVALID');
    return ring.map(([x,y])=>[wrap(x)/1e7,y/1e7]);
   });
  });
  const longitudes=[...xs].sort((a,b)=>a-b);let gap=-1,index=0;
  for(let i=0;i<longitudes.length;i++){const next=longitudes[(i+1)%longitudes.length]+(i+1===longitudes.length?3600000000:0),size=next-longitudes[i];if(size>=gap){gap=size;index=i;}}
  const bounds=[longitudes[(index+1)%longitudes.length],south,longitudes[index],north];
  if(bounds.some((value,i)=>value!==selected.bounds[i]))fail('TRACK_REGION_BOUNDARY_BOUNDS_MISMATCH');check(signal);
  return Object.freeze({...selected,boundaryVerified:true,boundaryHash:digest,geometry:{type:'MultiPolygon',coordinates:polygons}});
 }
 async function query(region,{kind=null,author=null,state=0,activity=null,tags=[],limit=Math.min(16,pageLimit),snapshot=null,cursor=null,signal}={}){
  check(signal);if(!validNode(region)||kind!==null&&!integer(kind,0,1)||author!==null&&!bytes(author,64)||state!==null&&!integer(state,0,1)||!integer(limit,1,pageLimit)||snapshot!==null&&!snapshotValid(snapshot)||cursor!==null&&(!Array.isArray(cursor)||cursor.length!==2||!bytes(cursor[0],32)||!bytes(cursor[1],32)||snapshot===null))fail('TRACK_REGION_QUERY_OPTIONS_INVALID');
  let labels;try{labels=validateTrackEditorMetadata({activity:activity??0,tags});if(activity!==null&&!integer(activity,0,255))fail('TRACK_REGION_QUERY_OPTIONS_INVALID');}catch{fail('TRACK_REGION_QUERY_OPTIONS_INVALID');}
  const metadata=activity!==null||labels.tags.length>0;if(metadata&&!metadataSupported)fail('TRACK_QUERY_METADATA_UNSUPPORTED');
  const owner=author?.slice()??null,previous=cursor?.map(value=>value.slice())??null,binding=await Identity.fullHash(encode(metadata?['trailmate.track.region.query.metadata.v1',region.id,version,'intersects',kind,owner,state,activity,labels.tags]:[region.id,version,'intersects',kind,owner,state]));check(signal);
  if(previous&&!equal(previous[1],binding))fail('TRACK_REGION_QUERY_CURSOR_MISMATCH');
  const response=await request(metadata?16:13,metadata?[region.id,version.slice(),'intersects',kind,owner,state,activity,labels.tags,limit,snapshot,previous]:[region.id,version.slice(),'intersects',kind,owner,state,limit,snapshot,previous],{signal});check(signal);
  if(!Array.isArray(response)||response.length!==3||!snapshotValid(response[0])||!Array.isArray(response[2])||response[2].length>limit||snapshot!==null&&BigInt(snapshot)!==BigInt(response[0]))fail('TRACK_REGION_QUERY_RESPONSE_INVALID');
  const items=response[2].map(row=>validateSharedTrackSummary(row,{metadata}));let after=previous?.[0];
  for(const item of items){if(after&&compare(item.objectId,after)<=0||kind!==null&&kind!==item.kind||state!==null&&state!==item.state||owner&&!equal(owner,item.author)||activity!==null&&item.activity!==activity||metadata&&labels.tags.some(tag=>!item.tags.includes(tag))||!intersects(region.bounds,item.bounds))fail('TRACK_REGION_QUERY_FILTER_MISMATCH');after=item.objectId;}
  const next=response[1];if(next!==null&&(!Array.isArray(next)||next.length!==2||!bytes(next[0],32)||!bytes(next[1],32)||!equal(next[1],binding)||previous&&compare(next[0],previous[0])<=0||after&&compare(next[0],after)<0))fail('TRACK_REGION_QUERY_CURSOR_MISMATCH');
  return {snapshot:response[0],cursor:next?.map(value=>value.slice())??null,items};
 }
 return Object.freeze({capabilities:Object.freeze({namespace,version:version.slice(),depth,pageLimit,chunkBytes}),validateNode:node,children,region,query});
}
