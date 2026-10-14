import {chooseArchiveKml} from './kml-archive.js';
import {parseKml,KML_LIMIT} from './kml.js';
import {validateDraftAttachmentRows} from './draft-attachments.js';

const encoder=new TextEncoder(),NS='http://www.opengis.net/kml/2.2';
const mime=name=>({png:'image/png',jpg:'image/jpeg',jpeg:'image/jpeg',gif:'image/gif',webp:'image/webp',bmp:'image/bmp',svg:'image/svg+xml',kml:'application/vnd.google-earth.kml+xml',dae:'model/vnd.collada+xml',txt:'text/plain',json:'application/json'}[name.split('.').at(-1).toLowerCase()]??'application/octet-stream');
const portable=name=>'kmz/'+name.split('/').map(encodeURIComponent).join('/');
const fail=code=>{throw Object.assign(Error(code),{code});};

// Only the chosen KML is transformed. Other files keep their exact contents and
// directory relationship under kmz/. The immutable original is retained too.
export function prepareKmlArchiveDraft(archive,name){
 const selected=chooseArchiveKml(archive,name);
 if(archive.files.has('signed-manifest.cmp'))fail('KML_ARCHIVE_SIGNED');
 const model=parseKml(selected.kml),base='https://kmz.invalid/zip/'+selected.name.split('/').map(encodeURIComponent).join('/');
 const references=new Map(),missing=new Set(),external=new Set();let rewritten=0;
 const roles=new Map();
 function rewrite(raw,role){
  if(!raw||raw.startsWith('#'))return raw;
  if(/^[a-z][a-z0-9+.-]*:|^\/\//i.test(raw.trim())){external.add(raw);return raw;}
  let url,path;
  try{
   url=new URL(raw,base);
   if(url.origin!=='https://kmz.invalid'||!url.pathname.startsWith('/zip/'))throw Error('Outside archive');
   const parts=url.pathname.slice(5).split('/').map(decodeURIComponent);
   if(parts.some(part=>!part||part==='.'||part==='..'||/[\/\\:\x00-\x1f\x7f]/.test(part)))throw Error('Invalid path');
   path=parts.join('/');
  }catch{missing.add(raw);return raw;}
  if(path===selected.name&&url.hash){if(url.hash!==raw)rewritten++;return url.hash;}
  if(!archive.files.has(path)){missing.add(raw);return raw;}
  const result=portable(path)+url.search+url.hash;
  if(!references.has(path))references.set(path,new Set());references.get(path).add(result);
  roles.set(result,Math.min(roles.get(result)??3,role));
  if(result!==raw)rewritten++;
  return result;
 }
 for(const node of model.xml.getElementsByTagNameNS(NS,'href')){
  const parent=node.parentElement,role=parent?.localName==='Icon'?(parent.parentElement?.localName==='IconStyle'?1:2):3;
  node.textContent=rewrite(node.textContent,role);
 }
 for(const node of model.xml.getElementsByTagNameNS(NS,'styleUrl'))node.textContent=rewrite(node.textContent,3);
 for(const node of model.xml.getElementsByTagNameNS(NS,'description')){
  const template=document.createElement('template');template.innerHTML=node.textContent;let changed=false;
  for(const element of template.content.querySelectorAll('[src],[href]'))for(const attribute of ['src','href']){
   if(!element.hasAttribute(attribute))continue;const before=element.getAttribute(attribute),after=rewrite(before,element.tagName==='IMG'?0:3);
   if(after!==before){element.setAttribute(attribute,after);changed=true;}
  }
  if(changed)node.textContent=template.innerHTML;
 }
 const source=rewritten?encoder.encode(new XMLSerializer().serializeToString(model.xml)):selected.kml.slice();
 if(source.length>KML_LIMIT.bytes)fail('KML_ARCHIVE_LIMIT');parseKml(source);
 const attachments=[];
 for(const [path,content] of archive.files){
  // Keep all members, including unreferenced resources. Aliases differing by
  // query/fragment also need their precise reference in the signed index.
  const names=new Set([portable(path),...(references.get(path)??[])]);
  for(const reference of names){
   const type=mime(path),role=roles.get(reference)??3;
   attachments.push({id:'kmz-'+String(attachments.length+1).padStart(4,'0'),reference,mime:type,role,required:references.get(path)?.has(reference)??false,content});
  }
 }
 validateDraftAttachmentRows(attachments);
 const filename=(Array.from(selected.name.split('/').at(-1).replace(/\.kml$/i,'')).slice(0,30).join('')||'track')+'.kml';
 return {source,originalKml:selected.kml,attachments,filename,member:selected.name,rewritten,missing:[...missing],external:[...external],model:parseKml(source)};
}
