import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {createTrackRegionClient} from './region-client.js';
const failure=code=>Object.assign(Error(code),{code});
export class RegionUsbContext{
 #binding;#client;#nodes=new Map();#boundary;#reply;
 constructor(binding){this.#binding=binding;}
 get binding(){return this.#binding;}
 #node(id){const node=this.#nodes.get(id);if(!node)throw failure('TRACK_REGION_REFERENCE_UNVERIFIED');this.#nodes.delete(id);this.#nodes.set(id,node);return node;}
 #remember(node){this.#nodes.delete(node.id);this.#nodes.set(node.id,node);while(this.#nodes.size>256)this.#nodes.delete(this.#nodes.keys().next().value);}
 parameters(operation,body){
  if(operation===9){if(body.length)throw failure('TRACK_REGION_OPTIONS_INVALID');return [];}
  if(!this.#client)throw failure('TRACK_REGION_REFERENCE_UNVERIFIED');
  const {namespace,version,depth,pageLimit,chunkBytes}=this.#client.capabilities;
  const id=value=>typeof value==='string'&&value.startsWith(namespace+':')&&/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(value.slice(namespace.length+1));
  if(operation===10){
   if(body.length!==4||!equal(body[0],version)||body[1]!==null&&!id(body[1])||body[2]!==null&&!id(body[2])||!integer(body[3],1,pageLimit))throw failure('TRACK_REGION_OPTIONS_INVALID');
   const parent=body[1]===null?null:this.#node(body[1]);if(body[2]!==null&&this.#node(body[2]).parent!==body[1])throw failure('TRACK_REGION_OPTIONS_INVALID');
   return [namespace,version.slice(),body[1],parent?.level??-1,body[2],depth,body[3]];
  }
  if(operation===11){if(body.length!==2||!id(body[0])||!equal(body[1],version))throw failure('TRACK_REGION_OPTIONS_INVALID');return [namespace,body[0],version.slice(),depth];}
  if(operation===12){
   const bound=this.#boundary;
   if(body.length!==4||!bound||body[0]!==bound.id||!equal(body[1],version)||!integer(body[2],0,bound.bytes-1)||!integer(body[3],1,chunkBytes)||body[3]>bound.bytes-body[2])throw failure('TRACK_REGION_REFERENCE_UNVERIFIED');
   return [namespace,bound.id,version.slice(),bound.hash.slice(),bound.bytes,body[2],body[3]];
  }
  if(operation===13){
   if(body.length!==9||!id(body[0])||!equal(body[1],version)||body[2]!=='intersects')throw failure('TRACK_REGION_OPTIONS_INVALID');
   const selected=this.#node(body[0]);return [namespace,selected.id,version.slice(),selected.bounds.slice(),...body.slice(3)];
  }
  throw failure('TRACK_DIRECTORY_OPERATION_INVALID');
 }
 async learn(operation,body,result,{signal}={}){
  if(operation===9){
   const client=await createTrackRegionClient(async()=>result,{signal});
   if(!this.#client||this.#client.capabilities.namespace!==client.capabilities.namespace||!equal(this.#client.capabilities.version,client.capabilities.version)){this.#nodes.clear();this.#boundary=undefined;}
   this.#client=client;return;
  }
  const client=this.#client;if(!client)throw failure('TRACK_REGION_REFERENCE_UNVERIFIED');
  const {version}=client.capabilities;
  if(operation===10){
   const parent=body[1]===null?null:this.#node(body[1]);
   // Reuse the protocol validator with a single authenticated response. This
   // does not issue another USB request or retain a second transport session.
   const verifier=await createTrackRegionClient(async(op)=>op===9?[1,'trailmate.track.regions.v1',client.capabilities.namespace,version,client.capabilities.depth,client.capabilities.pageLimit,client.capabilities.chunkBytes,[9,10,11,12,13]]:result,{signal});
   const page=await verifier.children(parent,{after:body[2],limit:body[3],signal});for(const node of page.items)this.#remember(node);
  }else if(operation===11){
   if(!Array.isArray(result)||result.length!==3||!equal(result[0],version)||!Array.isArray(result[2])||result[2].length!==3||result[2][0]!=='region-e7-json-v1'||!bytes(result[2][1],32)||!integer(result[2][2],1,8388608))throw failure('TRACK_REGION_DESCRIPTOR_INVALID');
   const node=client.validateNode(result[1]);if(node.id!==body[0])throw failure('TRACK_REGION_DESCRIPTOR_INVALID');this.#remember(node);this.#boundary={id:node.id,hash:result[2][1].slice(),bytes:result[2][2]};
  }else if(operation===12){
   const bound=this.#boundary;
   if(!Array.isArray(result)||result.length!==4||!bound||!equal(result[0],bound.hash)||result[1]!==bound.bytes||result[2]!==body[2]||!bytes(result[3],body[3]))throw failure('TRACK_REGION_BOUNDARY_CHUNK_INVALID');
  }
 }
}
