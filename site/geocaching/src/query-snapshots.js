import {bytes,equal,integer} from './protocol.js';

// Only authenticated, capability-admitted query replies create leases here.
// Maps normally contain <=6 entries (3 sources x 2 date-line query boxes).
export function rememberQuerySnapshot(client,page,result){
 if(client.closed||!page.directory.queryReleaseSupported)return;
 const key=page.directory.key+':'+Array.from(result[0],value=>value.toString(16).padStart(2,'0')).join('');
 if(result[2]===null){client.querySnapshots.delete(key);return;}
 if(!client.querySnapshots.has(key))client.querySnapshots.set(key,{directory:page.directory,snapshot:result[0].slice(),epoch:page.directory.epoch.slice(),requestId:null});
}

export async function releaseQuerySnapshots(client){
 for(const [key,lease] of client.querySnapshots){
  if(client.closed)throw Error('Connection closed');
  const entry=client.directories.get(lease.directory.key);
  if(entry!==lease.directory||!entry.ready||!entry.queryReleaseSupported||!equal(entry.epoch,lease.epoch)){
   // A lost/replaced service cannot be safely addressed as the original one.
   // Its old lease keeps its server-side expiry; do not target another epoch.
   client.querySnapshots.delete(key);continue;
  }
  lease.requestId??=crypto.getRandomValues(new Uint8Array(16));
  const result=await client.request(entry,6,[lease.snapshot.slice()],{requestId:lease.requestId.slice()});
  if(!Array.isArray(result)||result.length!==2||!bytes(result[0],16)||!equal(result[0],lease.snapshot)||!integer(result[1],0,1))throw Error('Invalid query release response');
  // Retain the lease/ID on errors or lost replies so retry cannot amplify work.
  if(client.querySnapshots.get(key)===lease)client.querySnapshots.delete(key);
 }
}
