import {TmsDocument} from './tms-document.js';
import {configurationChanges} from './config-collections.js';
const fingerprint=author=>author?.verified===true&&/^[0-9a-f]{64}$/i.test(author.fingerprint??'')?author.fingerprint.toLowerCase():undefined;
export function createConfigurationReadback(document,author){
 if(!(document instanceof TmsDocument))throw Error('Invalid saved configuration');
 return {expected:new TmsDocument(document.serialize({kind:'working'})),fingerprint:fingerprint(author)};
}
export function checkConfigurationReadback(check,actual,author,hash,contentSha256){
 if((!(check?.expected instanceof TmsDocument)&&!check?.sha256)||!(actual instanceof TmsDocument))throw Error('Invalid configuration readback');
 const current=fingerprint(author);
 if(!check.fingerprint||!current)return {status:'unverified',keys:[]};
 if(check.fingerprint!==current)return {status:'wrongDevice',keys:[]};
 if(!check.expected){
  if(check.version===2){
   if(!/^[0-9a-f]{64}$/.test(contentSha256??''))throw Error('Missing verified content hash');
   return {status:contentSha256===check.contentSha256?'matched':'mismatchSummary',keys:[]};
  }
  if(!(hash instanceof Uint8Array)||hash.length!==32)throw Error('Missing verified snapshot hash');
  const actualHash=Array.from(hash,byte=>byte.toString(16).padStart(2,'0')).join('');
  return {status:actualHash===check.sha256?'matched':'mismatchSummary',keys:[]};
 }
 const keys=[...configurationChanges(actual,check.expected)];
 return {status:keys.length?'mismatch':'matched',keys};
}
