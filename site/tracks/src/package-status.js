import {equal,integer,bytes} from '../../geocaching/src/protocol.js';
// Availability is a current directory assertion, not author/file verification
// or proof that the device can render/navigate the installed content.
export function validateTrackPackageStatus(response,revisionHash,index){
 if(!Array.isArray(response)||response.length!==6||response[0]!==1||!bytes(revisionHash,32)||!equal(response[1],revisionHash)||
  ![response[2],response[3],response[4]].every(value=>integer(value,0,1))||!Array.isArray(response[5]))throw Error('Invalid track package status');
 const entries=index?.[3]??[],rows=response[5];
 if(response[3]===0){if(rows.length||response[4]!==0)throw Error('Invalid track package status');return {kmlAvailable:response[2]===1,indexAvailable:false,requiredComplete:false,allComplete:false,rows:[]};}
 if(rows.length!==entries.length)throw Error('Invalid track package status');
 let required=response[2]===1,all=required;
 const retained=rows.map((row,i)=>{
  const entry=entries[i];if(!Array.isArray(row)||row.length!==5||row[0]!==entry[0]||!equal(row[1],entry[1])||row[2]!==entry[2]||row[3]!==entry[6]||!integer(row[4],0,1))throw Error('Invalid track package status');
  if(!row[4]){all=false;if(row[3])required=false;}
  return {id:row[0],hash:row[1].slice(),bytes:row[2],required:row[3]===1,available:row[4]===1};
 });
 if((response[4]===1)!==required)throw Error('Invalid track package status');
 return {kmlAvailable:response[2]===1,indexAvailable:true,requiredComplete:required,allComplete:all,rows:retained};
}
