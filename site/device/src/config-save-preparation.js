import {TmsDocument} from './tms-document.js';
import {configCollectionIssues} from './config-collections.js';
import {validateKnownModuleDocument} from './config-module-validation.js';
import {validateC6Configuration} from './config-c6-validation.js';
import {synchronizeMeshCoreChannelMirror} from './config-editor-values.js';

// Match the production TMS writer: disabled interface access is represented by
// the absence of all three records. Never drop partial, malformed or live access.
export function normalizeDisabledInterfaceAccess(document){
 const entries=new Map(document.entries().map(record=>[record.key,record])),count=entries.get('rt.net.interface_count'),omitted=[];
 if(count?.type==='u8'&&Number.isInteger(count.value)&&count.value>=0&&count.value<=6){
  for(let index=0;index<count.value;index++){
   const prefix=`rt.net.interface.${index}.`,name=entries.get(prefix+'network_name'),passphrase=entries.get(prefix+'passphrase'),size=entries.get(prefix+'ifac_size');
   if(name?.type==='str'&&name.value===''&&passphrase?.type==='str'&&passphrase.value===''&&size?.type==='u16'&&size.value===0)omitted.push(name.key,passphrase.key,size.key);
  }
 }
 const source=document.serialize({kind:'working'});
 if(!omitted.length)return {document:new TmsDocument(source),source,omitted};
 const removed=new Set(omitted),text=new TextDecoder('utf-8',{fatal:true}).decode(source),lines=text.split('\n');
 const bytes=new TextEncoder().encode(lines.filter(line=>!removed.has(line.slice(0,line.indexOf('=')))).join('\n'));
 return {document:new TmsDocument(bytes),source:bytes,omitted};
}

// Review, transfer, transaction retries and readback all use these same bytes.
// Work on a clone so validation/approval failures leave the user's form intact.
export function prepareConfigurationSave(document){
 const candidate=new TmsDocument(document.serialize({kind:'working'}));
 synchronizeMeshCoreChannelMirror(candidate);
 const issues=configCollectionIssues(candidate);
 if(issues.length)throw Error(issues.map(issue=>`${issue.key}：${issue.message}`).join('；'));
 validateKnownModuleDocument(candidate);
 validateC6Configuration(candidate);
 return normalizeDisabledInterfaceAccess(candidate);
}
