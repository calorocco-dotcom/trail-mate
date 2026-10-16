let network;
// Registered by the existing map page. Maintenance shares its verified browser
// IP connection instead of opening another stack or querying device RF state.
export function registerGeocachingNetwork({rpc,isReady}){
 if(typeof rpc!=='function'||typeof isReady!=='function')throw TypeError('Invalid browser network bridge');
 const current=Object.freeze({
  ready:()=>isReady()===true,
  directories:()=>{if(!isReady())return Promise.reject(Error('Browser network unavailable'));return rpc('directories');},
  current:(cacheId,directoryKey)=>{if(!isReady())return Promise.reject(Error('Browser network unavailable'));return rpc('current',{cacheId,directoryKey});},
  publish:(signed,authorPublicKey,directoryKey)=>{if(!isReady())return Promise.reject(Error('Browser network unavailable'));return rpc('publish',{signed,authorPublicKey,directoryKey});},
  publicationReceipt:(signed,authorPublicKey,directoryKey)=>{if(!isReady())return Promise.reject(Error('Browser network unavailable'));return rpc('publicationReceipt',{signed,authorPublicKey,directoryKey});},
 });
 network=current;
 return ()=>{if(network===current)network=undefined;};
}
export function getGeocachingNetwork(){return network;}
