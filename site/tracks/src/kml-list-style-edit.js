import {parseKml} from './kml.js';
const NS='http://www.opengis.net/kml/2.2',utf8=new TextEncoder();
export const KML_LIST_TYPES=['check','radioFolder','checkOffOnly','checkHideChildren'];
export const KML_LIST_STATES=['open','closed','error','fetching0','fetching1','fetching2'];
const fail=code=>{throw Object.assign(Error(code),{code:'KML_LIST_STYLE_'+code.toUpperCase()});};
const children=(node,name)=>[...(node?.children??[])].filter(child=>child.namespaceURI===NS&&child.localName===name);
const text=(node,name)=>children(node,name)[0]?.textContent;
const ownsId=node=>[node,...node.getElementsByTagName('*')].some(value=>value.hasAttribute('id'));
function active(node){for(let parent=node;parent;parent=parent.parentElement)if(parent.namespaceURI!==NS||['Update','ExtendedData','Metadata'].includes(parent.localName))return false;return true;}
function styles(model){return [...model.xml.getElementsByTagNameNS(NS,'ListStyle')].filter(node=>node.parentElement?.namespaceURI===NS&&node.parentElement.localName==='Style'&&active(node));}
function containers(model){const nodes=[];function walk(node){if(node.namespaceURI!==NS)return;if(['Document','Folder'].includes(node.localName))nodes.push(node);if(['kml','Document','Folder'].includes(node.localName))for(const child of node.children)walk(child);}walk(model.xml.documentElement);return nodes;}
function path(node){const names=[];for(let parent=node;parent&&parent.localName!=='kml';parent=parent.parentElement){if(parent.namespaceURI!==NS)continue;const name=text(parent,'name')?.trim(),id=parent.getAttribute('id'),branch=parent.localName==='Pair'?text(parent,'key')?.trim():null;names.unshift(name||id&&parent.localName+' #'+id||branch||parent.localName);}return names.join(' / ');}
export function collectKmlListStyles(model){return styles(model).map((node,index)=>({index,path:path(node),listItemType:text(node,'listItemType')?.trim(),bgColor:text(node,'bgColor')?.trim(),icons:children(node,'ItemIcon').map((icon,index)=>({index,state:text(icon,'state')??'',href:text(icon,'href')??'',protected:ownsId(icon)}))}));}
export function collectKmlListContainers(model){return containers(model).map((node,index)=>({index,path:path(node)}));}
function scalar(node,key,value){const entries=children(node,key);if(entries.length>1)fail('ambiguous');const old=entries[0];if(value===null){if(old&&ownsId(old))fail('referenced');old?.remove();return;}if(old){old.textContent=value;return;}const fresh=node.ownerDocument.createElementNS(NS,key);fresh.textContent=value;const order=['listItemType','bgColor','ItemIcon','state','href'];const next=[...node.children].find(child=>child.namespaceURI===NS&&order.indexOf(child.localName)>order.indexOf(key));node.insertBefore(fresh,next??null);}
function string(value){if(typeof value!=='string'||utf8.encode(value).length>4096||/[\u0000-\u0008\u000b\u000c\u000e-\u001f\ufffe\uffff]/.test(value)||/[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/.test(value))fail('invalid');return value;}
function state(value){string(value);const tokens=value.trim().split(/\s+/).filter(Boolean);if(!tokens.length||tokens.some(token=>!KML_LIST_STATES.includes(token))||new Set(tokens).size!==tokens.length)fail('invalid');return tokens.join(' ');}
export function editKmlListStyle(source,index,changes){
 const model=parseKml(source),node=styles(model)[index];if(!Number.isInteger(index)||!node||!changes||typeof changes!=='object'||Array.isArray(changes)||Object.keys(changes).some(key=>!['listItemType','bgColor','icons'].includes(key)))fail('selection');
 const before=new XMLSerializer().serializeToString(node);
 if('listItemType'in changes){if(changes.listItemType!==null&&!KML_LIST_TYPES.includes(changes.listItemType))fail('invalid');scalar(node,'listItemType',changes.listItemType);}
 if('bgColor'in changes){if(changes.bgColor!==null&&(typeof changes.bgColor!=='string'||! /^[a-f\d]{8}$/i.test(changes.bgColor)))fail('invalid');scalar(node,'bgColor',changes.bgColor?.toLowerCase()??null);}
 if('icons'in changes){const icons=children(node,'ItemIcon'),seen=new Set(),deleted=[],added=[];if(!Array.isArray(changes.icons)||changes.icons.length>1024)fail('limit');
  for(const change of changes.icons){if(!change||typeof change!=='object'||Array.isArray(change)||Object.keys(change).some(key=>!['index','state','href','remove'].includes(key)))fail('selection');const fresh=change.index===null;if(!fresh&&(!Number.isInteger(change.index)||change.index<0||change.index>=icons.length||seen.has(change.index)))fail('selection');if(!fresh)seen.add(change.index);const icon=fresh?model.xml.createElementNS(NS,'ItemIcon'):icons[change.index];
   if('remove'in change){if(change.remove!==true||fresh||'state'in change||'href'in change)fail('selection');if(ownsId(icon))fail('referenced');deleted.push(icon);continue;}
   if(fresh&&(!('state'in change)||!('href'in change)))fail('selection');if('state'in change)scalar(icon,'state',state(change.state));if('href'in change)scalar(icon,'href',string(change.href));if(fresh)added.push(icon);
  }if(icons.length-deleted.length+added.length>1024)fail('limit');deleted.forEach(icon=>icon.remove());added.forEach(icon=>node.append(icon));
 }
 if(new XMLSerializer().serializeToString(node)===before)return model.source;const output=utf8.encode(new XMLSerializer().serializeToString(model.xml));parseKml(output);return output;
}
export function createKmlContainerListStyle(source,index){
 const model=parseKml(source),node=containers(model)[index];if(!Number.isInteger(index)||!node)fail('selection');const style=model.xml.createElementNS(NS,'Style'),list=model.xml.createElementNS(NS,'ListStyle');style.append(list);
 // Feature style selectors precede Region/ExtendedData and container children.
 const before=[...node.children].find(child=>child.namespaceURI===NS&&['Region','Metadata','ExtendedData','Document','Folder','Placemark','NetworkLink','GroundOverlay','ScreenOverlay','PhotoOverlay'].includes(child.localName));node.insertBefore(style,before??null);const indexCreated=styles(model).indexOf(list),output=utf8.encode(new XMLSerializer().serializeToString(model.xml));parseKml(output);return{source:output,index:indexCreated};
}
