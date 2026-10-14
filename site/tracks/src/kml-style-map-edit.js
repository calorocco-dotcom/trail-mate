import {parseKml} from './kml.js';
import {removeSharedKmlStyleSelector} from './kml-style-identity-edit.js';
const NS='http://www.opengis.net/kml/2.2',encoder=new TextEncoder();
const fail=key=>{throw Object.assign(Error(key),{code:'KML_STYLE_MAP_'+key.toUpperCase()});};
const children=(node,name)=>[...(node?.children??[])].filter(child=>child.namespaceURI===NS&&child.localName===name);
const text=(node,name)=>children(node,name)[0]?.textContent.trim()??'';
const selector=node=>node.namespaceURI===NS&&['Style','StyleMap'].includes(node.localName);
function active(node){for(let parent=node;parent;parent=parent.parentElement)if(parent.namespaceURI===NS&&parent.localName==='Update')return false;return true;}
function fragment(url){if(!url?.startsWith('#'))return null;try{return decodeURIComponent(url.slice(1));}catch{return null;}}
function ids(model){const all=new Map();for(const node of model.xml.getElementsByTagName('*'))if(node.hasAttribute('id')){const id=node.getAttribute('id');if(!all.has(id))all.set(id,[]);all.get(id).push(node);}return all;}
function selected(model,id,kind){const nodes=ids(model).get(id);if(nodes?.length!==1||!selector(nodes[0])||!active(nodes[0])||kind&&nodes[0].localName!==kind)fail('ambiguous');return nodes[0];}
function one(node,name){const rows=children(node,name);if(rows.length>1)fail('ambiguous');return rows[0];}
function set(node,name,value){const child=one(node,name)??node.appendChild(node.ownerDocument.createElementNS(NS,name));child.textContent=value;return child;}
const hasId=node=>[node,...node.getElementsByTagName('*')].some(child=>child.hasAttribute('id'));
function statePair(map,state){const pairs=children(map,'Pair').filter(pair=>text(pair,'key')===state);if(pairs.length>1||pairs.some(pair=>children(pair,'key').length!==1))fail('ambiguous');return pairs[0];}
function features(model){const rows=[];function walk(node){if(node.namespaceURI!==NS)return;if(node.localName==='Placemark')rows.push(node);else if(['kml','Document','Folder'].includes(node.localName))[...node.children].forEach(walk);}walk(model.xml.documentElement);return rows;}
function feature(model,index){const rows=features(model);if(!Number.isInteger(index)||index<0||index>=rows.length)fail('selection');return rows[index];}
function validateReference(model,url){if(typeof url!=='string'||url!==url.trim()||!url||/[\u0000-\u0020\u007f]/.test(url))fail('reference');if(url.startsWith('#')){const id=fragment(url);if(!id)fail('reference');selected(model,id);}return url;}
function cycle(model,node){
 const lookup=ids(model);function visit(current,state,seen){if(!current)return;if(seen.has(current)||seen.size>=16)fail('cycle');if(current.localName!=='StyleMap')return;const next=new Set(seen);next.add(current);const pair=statePair(current,state)??statePair(current,'normal');if(!pair)return;for(const inline of [...pair.children].filter(selector))visit(inline,state,next);const id=fragment(text(pair,'styleUrl'));if(id){const targets=lookup.get(id);if(targets?.length===1&&selector(targets[0]))visit(targets[0],state,next);}}
 for(const state of ['normal','highlight'])visit(node,state,new Set());
}
function finish(model){const bytes=encoder.encode(new XMLSerializer().serializeToString(model.xml));parseKml(bytes);return bytes;}
function pairChanges(model,map,changes){
 if(!changes||typeof changes!=='object'||Array.isArray(changes)||Object.keys(changes).some(state=>!['normal','highlight'].includes(state)))fail('selection');
 for(const [state,value] of Object.entries(changes)){
  if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).some(key=>!['reference','keepInline','remove'].includes(key)))fail('selection');let pair=statePair(map,state);
  if(value.remove===true){if(Object.keys(value).length!==1)fail('selection');if(pair){if(hasId(pair))fail('referenced');pair.remove();}continue;}
  if(value.remove!==undefined||typeof value.keepInline!=='boolean')fail('selection');const reference=validateReference(model,value.reference);
  if(!pair){pair=model.xml.createElementNS(NS,'Pair');set(pair,'key',state);map.appendChild(pair);}else one(pair,'styleUrl');
  const inline=[...pair.children].filter(selector);if(!value.keepInline){if(inline.some(hasId))fail('referenced');inline.forEach(node=>node.remove());}
  set(pair,'styleUrl',reference);
 }
 cycle(model,map);
}
export function sharedKmlStyleSelectors(model){const all=ids(model);return [...model.xml.getElementsByTagName('*')].filter(node=>selector(node)&&active(node)&&node.getAttribute('id')).map(node=>({id:node.getAttribute('id'),type:node.localName,ambiguous:all.get(node.getAttribute('id')).length!==1}));}
export function sharedKmlStyleMaps(model){return sharedKmlStyleSelectors(model).filter(row=>row.type==='StyleMap').map(row=>{const node=ids(model).get(row.id)[0],pairs={};for(const state of ['normal','highlight']){const matches=children(node,'Pair').filter(pair=>text(pair,'key')===state),pair=matches[0];pairs[state]={present:Boolean(pair),reference:text(pair,'styleUrl'),inline:[...(pair?.children??[])].filter(selector).length,ambiguous:matches.length>1||Boolean(pair&&(children(pair,'key').length!==1||children(pair,'styleUrl').length>1))};}return {...row,pairs};});}
export function styleMapDependents(model,id){
 const maps=sharedKmlStyleSelectors(model),lookup=ids(model),dependents=new Set([id]),reverse=new Map();for(const row of maps){const node=lookup.get(row.id)[0];for(const ref of node.getElementsByTagNameNS(NS,'styleUrl')){const target=fragment(ref.textContent.trim());if(!target)continue;if(!reverse.has(target))reverse.set(target,new Set());reverse.get(target).add(row.id);}}const queue=[id];for(let cursor=0;cursor<queue.length;cursor++)for(const ref of reverse.get(queue[cursor])??[])if(!dependents.has(ref)){dependents.add(ref);queue.push(ref);}
 return features(model).flatMap((node,index)=>[...node.getElementsByTagNameNS(NS,'styleUrl')].some(ref=>dependents.has(fragment(ref.textContent.trim())))?[{index,name:model.features[index]?.name??'',path:model.features[index]?.path??[]}]:[]);
}
export function editSharedKmlStyleMap(source,id,changes){const model=parseKml(source),node=selected(model,id,'StyleMap');if(!Object.keys(changes??{}).length)return model.source;pairChanges(model,node,changes);return finish(model);}
export function appendSharedKmlSelector(model,kind,id){
 if(!['Style','StyleMap'].includes(kind)||typeof id!=='string'||! /^[\p{L}_][\p{L}\p{N}\p{M}_.-]*$/u.test(id)||ids(model).has(id))fail('id');const root=model.xml.documentElement;let parent=children(root,'Document')[0];if(!parent){parent=model.xml.createElementNS(NS,'Document');const rows=[...root.children].filter(node=>node.namespaceURI===NS&&['Folder','Placemark','NetworkLink','GroundOverlay','ScreenOverlay','PhotoOverlay'].includes(node.localName));root.appendChild(parent);rows.forEach(node=>parent.appendChild(node));}
 const node=model.xml.createElementNS(NS,kind);node.setAttribute('id',id);const before=[...parent.children].find(node=>node.namespaceURI===NS&&['Region','Metadata','ExtendedData','Schema','Folder','Placemark','NetworkLink','GroundOverlay','ScreenOverlay','PhotoOverlay'].includes(node.localName));parent.insertBefore(node,before??null);return node;
}
export function createSharedKmlStyleMap(source,id,changes){const model=parseKml(source),map=appendSharedKmlSelector(model,'StyleMap',id);pairChanges(model,map,changes);return finish(model);}
export function getSharedKmlStyleMap(model,id){return selected(model,id,'StyleMap');}
export function bindKmlFeatureStyleMap(source,index,id,{keepInline=true}={}){const model=parseKml(source);selected(model,id,'StyleMap');const node=feature(model,index);one(node,'styleUrl');if(typeof keepInline!=='boolean')fail('selection');if(!keepInline){const inline=[...node.children].filter(selector);if(inline.some(hasId))fail('referenced');inline.forEach(child=>child.remove());}let ref=one(node,'styleUrl');if(!ref){ref=model.xml.createElementNS(NS,'styleUrl');const before=[...node.children].find(child=>selector(child)||child.namespaceURI===NS&&['Region','ExtendedData','Point','LineString','LinearRing','Polygon','MultiGeometry','Model'].includes(child.localName)||child.namespaceURI==='http://www.google.com/kml/ext/2.2'&&['Track','MultiTrack'].includes(child.localName));node.insertBefore(ref,before??null);}ref.textContent='#'+encodeURIComponent(id);return finish(model);}
export function removeSharedKmlStyleMap(source,id){try{return removeSharedKmlStyleSelector(source,id,'StyleMap');}catch(error){const code=error.code==='KML_STYLE_ID_AMBIGUOUS'?'ambiguous':'referenced';fail(code);}}
