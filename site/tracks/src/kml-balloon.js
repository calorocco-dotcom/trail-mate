import {kmlColor} from './kml-style.js';
const NS='http://www.opengis.net/kml/2.2',encode=new TextEncoder();
export const BALLOON_PREVIEW_LIMIT=Object.freeze({bytes:65536,output:32768,variables:256,nodes:4096,depth:32});
const children=(node,name)=>[...(node?.children??[])].filter(value=>value.namespaceURI===NS&&value.localName===name);
const value=(node,name)=>children(node,name)[0]?.textContent??'';
const escape=value=>String(value).replace(/[&<>"']/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
// Inert template content is never attached. This preview displays text only;
// markup is retained in the saved template, without running it or fetching URLs.
export function kmlBalloonPlainText(html){
 const limits=BALLOON_PREVIEW_LIMIT;if(typeof html!=='string'||encode.encode(html).length>limits.bytes)throw Object.assign(Error('Balloon preview limit'),{code:'KML_BALLOON_PREVIEW_LIMIT'});
 const template=document.createElement('template');template.innerHTML=html;let count=0,length=0;const output=[];
 const add=text=>{length+=text.length;if(length>limits.output)throw Object.assign(Error('Balloon preview limit'),{code:'KML_BALLOON_PREVIEW_LIMIT'});output.push(text);};
 function walk(node,depth){if(++count>limits.nodes||depth>limits.depth)throw Object.assign(Error('Balloon preview limit'),{code:'KML_BALLOON_PREVIEW_LIMIT'});if(node.nodeType===3){add(node.textContent);return;}if(node.nodeType!==1&&node.nodeType!==11)return;
  if(node.nodeType===1&&['SCRIPT','STYLE','IFRAME','OBJECT','EMBED','SVG','MATH','TEMPLATE','NOSCRIPT'].includes(node.tagName))return;
  if(node.tagName==='BR'){add('\n');return;}if(node.tagName==='IMG'){if(node.getAttribute('alt'))add(node.getAttribute('alt'));return;}
  const block=['P','DIV','LI','TR','H1','H2','H3','H4','H5','H6','PRE','BLOCKQUOTE'].includes(node.tagName);if(block)add('\n');for(const child of node.childNodes)walk(child,depth+1);if(block)add('\n');
 }walk(template.content,0);return output.join('').replace(/\n{3,}/g,'\n\n').trim();
}
export function kmlBalloonVariables(model,index){
 const feature=model.features[index];if(!feature)return [];
 const nodes=[];function walk(node){if(node.namespaceURI!==NS)return;if(node.localName==='Placemark')nodes.push(node);else if(['kml','Document','Folder'].includes(node.localName))for(const child of node.children)walk(child);}walk(model.xml.documentElement);const node=nodes[index];
 const map=new Map(),put=(key,text,rich=false)=>{if(map.has(key)){map.set(key,null);return;}map.set(key,{key,text,rich});};
 for(const [key,text,rich]of [['name',feature.name,false],['description',feature.description,true],['id',feature.id??'',false],['address',value(node,'address'),false],['Snippet',value(node,'Snippet'),true]])put(key,text,rich);
 for(const row of feature.extendedData){if(!row.name)continue;if(!row.schemaUrl){put(row.name,row.value);put(row.name+'/displayName',row.displayName||row.name,true);continue;}
  if(!row.schemaUrl.startsWith('#'))continue;let id;try{id=decodeURIComponent(row.schemaUrl.slice(1));}catch{continue;}const schemas=[...model.xml.getElementsByTagName('*')].filter(node=>node.getAttribute('id')===id);if(schemas.length!==1||schemas[0].namespaceURI!==NS||schemas[0].localName!=='Schema')continue;const schema=schemas[0],name=schema.getAttribute('name');if(!name)continue;const fields=children(schema,'SimpleField').filter(field=>field.getAttribute('name')===row.name);if(fields.length!==1)continue;put(name+'/'+row.name,row.value);put(name+'/'+row.name+'/displayName',value(fields[0],'displayName')||row.name,true);
 }
 return [...map.values()].filter(Boolean);
}
export function previewKmlBalloon(model,index,fields={}){
 const variables=kmlBalloonVariables(model,index),lookup=new Map(variables.map(value=>[value.key,value])),unknown=new Set(),limits=BALLOON_PREVIEW_LIMIT;
 const template=fields.text??'$[name]\n$[description]';let replacements=0;
 if(typeof template!=='string'||encode.encode(template).length>limits.bytes)throw Object.assign(Error('Balloon preview limit'),{code:'KML_BALLOON_PREVIEW_LIMIT'});
 const expanded=template.replace(/\$\[([^\]\r\n]{1,256})\]/g,(token,key)=>{if(++replacements>limits.variables)throw Object.assign(Error('Balloon preview limit'),{code:'KML_BALLOON_PREVIEW_LIMIT'});const found=lookup.get(key);if(!found){unknown.add(token);return escape(token);}return found.rich?found.text:escape(found.text);});
 const invalid=[];const color=(raw,fallback)=>{try{return kmlColor(raw??fallback);}catch{invalid.push(raw);return kmlColor(fallback);}};
 return {text:kmlBalloonPlainText(expanded),hidden:fields.displayMode==='hide',background:color(fields.bgColor,'ffffffff'),foreground:color(fields.textColor,'ff000000'),unknown:[...unknown],invalid};
}
