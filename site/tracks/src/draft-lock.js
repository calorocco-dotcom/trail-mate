export async function withTrackDraftLock(id,work,{locks=globalThis.navigator?.locks}={}){
 if(typeof id!=='string'||!id||typeof work!=='function'||!locks?.request)throw Error('Browser track draft locking unavailable');
 const digest=new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(id)));
 const name='trailmate-track-draft-'+Array.from(digest,byte=>byte.toString(16).padStart(2,'0')).join('');
 return locks.request(name,{mode:'exclusive',ifAvailable:true},lock=>{
  if(!lock)throw Object.assign(Error('Another tab owns this track draft'),{code:'TRACK_IP_DRAFT_BUSY'});
  return work();
 });
}
