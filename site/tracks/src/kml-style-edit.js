import {parseKml} from './kml.js';
import {createKmlStyleResolver,kmlStyleFields} from './kml-style.js';
import {validateKmlIconCrop} from './kml-icon-crop.js';
import {appendSharedKmlSelector,getSharedKmlStyleMap} from './kml-style-map-edit.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',utf8=new TextEncoder();
const tags={line:'LineStyle',poly:'PolyStyle',icon:'IconStyle',label:'LabelStyle',balloon:'BalloonStyle'};
const allowed={line:['color','colorMode','width'],poly:['color','colorMode','fill','outline'],icon:['color','colorMode','scale','heading','image','hotSpot'],label:['color','colorMode','scale'],balloon:['bgColor','textColor','text','displayMode']};
const fail=key=>{throw Object.assign(Error(key),{code:'KML_STYLE_EDIT_'+key.toUpperCase()});};
const children=(node,name,ns=NS)=>[...(node?.children??[])].filter(n=>n.namespaceURI===ns&&n.localName===name);
const raw=(node,name)=>children(node,name)[0]?.textContent.trim();
function unique(node,name,ns=NS){const nodes=children(node,name,ns);if(nodes.length>1)fail('ambiguous');return nodes[0];}
function features(model){const result=[];function walk(node){if(node.namespaceURI!==NS)return;if(node.localName==='Placemark')result.push(node);else if(['kml','Document','Folder'].includes(node.localName))[...node.children].forEach(walk);}walk(model.xml.documentElement);return result;}
function feature(model,index){const nodes=features(model);if(!Number.isInteger(index)||index<0||index>=nodes.length)fail('selection');return nodes[index];}
const rank=['IconStyle','LabelStyle','LineStyle','PolyStyle','BalloonStyle','ListStyle','color','colorMode','scale','heading','width','fill','outline','Icon','hotSpot','bgColor','textColor','text','displayMode','href','refreshMode','refreshInterval','viewRefreshMode','viewRefreshTime','viewBoundScale','viewFormat','httpQuery','x','y','w','h'];
function element(node,name,ns=NS){const old=unique(node,name,ns);if(old)return old;const child=node.ownerDocument.createElementNS(ns,ns===GX?(node.lookupPrefix(GX)||'gx')+':'+name:name);const next=[...node.children].find(n=>(n.namespaceURI===NS||n.namespaceURI===GX)&&rank.indexOf(n.localName)>rank.indexOf(name));node.insertBefore(child,next??null);return child;}
function field(node,name,value,ns=NS){element(node,name,ns).textContent=String(value);}
function numeric(value,min=0,max=Infinity){const raw=String(value).trim();if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(raw)||!Number.isFinite(Number(raw))||Number(raw)<min||Number(raw)>max)fail('number');return raw;}
function patch(style,changes){
 if(!changes||typeof changes!=='object'||Array.isArray(changes))fail('selection');
 for(const [kind,fields] of Object.entries(changes)){
  if(!tags[kind]||!fields||typeof fields!=='object'||Array.isArray(fields)||Object.keys(fields).some(key=>!allowed[kind].includes(key)))fail('selection');
  if(!Object.keys(fields).length)continue;const node=element(style,tags[kind]);
  for(const [key,value] of Object.entries(fields)){
   if(['color','bgColor','textColor'].includes(key)){if(typeof value!=='string'||!/^[\da-f]{8}$/i.test(value))fail('color');field(node,key,value.toLowerCase());}
   else if(key==='displayMode'){if(!['default','hide'].includes(value))fail('selection');field(node,key,value);}
   else if(key==='text'){if(typeof value!=='string'||utf8.encode(value).length>65536||/[\u0000-\u0008\u000b\u000c\u000e-\u001f\ufffe\uffff]/.test(value)||/[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/.test(value))fail('text');field(node,key,value);}
   else if(key==='colorMode'){if(!['normal','random'].includes(value))fail('selection');field(node,key,value);}
   else if(['width','scale','heading'].includes(key))field(node,key,numeric(value,0,key==='heading'?360:Infinity));
   else if(['fill','outline'].includes(key)){if(typeof value!=='boolean')fail('selection');field(node,key,value?'1':'0');}
   else if(key==='image'){
    if(!value||typeof value.href!=='string'||/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(value.href))fail('image');const icon=element(node,'Icon');field(icon,'href',value.href);
    if(value.crop===null){for(const key of ['x','y','w','h'])children(icon,key,GX).forEach(child=>child.remove());}
    else{if(!value.crop||['x','y','w','h'].some(key=>! /^[+-]?\d+$/.test(String(value.crop[key]).trim())))fail('crop');const crop=Object.fromEntries(['x','y','w','h'].map(key=>[key,Number(value.crop[key])]));try{validateKmlIconCrop(crop);}catch{fail('crop');}for(const [key,n] of Object.entries(crop))field(icon,key,n,GX);}
   }else if(key==='hotSpot'){
    if(!value||!['fraction','pixels','insetPixels'].includes(value.xunits)||!['fraction','pixels','insetPixels'].includes(value.yunits))fail('anchor');const x=numeric(value.x,-Infinity),y=numeric(value.y,-Infinity),anchor=element(node,'hotSpot');for(const [key,n] of Object.entries({x,y,xunits:value.xunits,yunits:value.yunits}))anchor.setAttribute(key,n);
   }
  }
 }
}
const hasId=node=>[node,...node.getElementsByTagName('*')].some(n=>n.hasAttribute('id'));
function mapFor(node){
 const selectors=[...node.children].filter(n=>n.namespaceURI===NS&&['Style','StyleMap'].includes(n.localName));let map=selectors.at(-1);
 const pairs=map?.localName==='StyleMap'?children(map,'Pair'):[];
 const usable=map?.localName==='StyleMap'&&!hasId(map)&&pairs.every(pair=>children(pair,'key').length===1&&children(pair,'Style').length<=1&&!children(pair,'StyleMap').length)&&['normal','highlight'].every(key=>pairs.filter(pair=>raw(pair,'key')===key).length<=1);
 if(!usable){map=node.ownerDocument.createElementNS(NS,'StyleMap');const next=[...node.children].find(n=>n.namespaceURI===NS&&['Region','ExtendedData','Point','LineString','LinearRing','Polygon','MultiGeometry','Model'].includes(n.localName)||n.namespaceURI===GX&&['Track','MultiTrack'].includes(n.localName));node.insertBefore(map,next??null);}
 // A missing highlight falls back to normal. Capture it before editing either
 // branch, so changing normal does not silently modify highlight (or vice versa).
 for(const state of ['normal','highlight'])if(!children(map,'Pair').some(pair=>raw(pair,'key')===state)){
  const normal=children(map,'Pair').find(pair=>raw(pair,'key')==='normal'),pair=state==='highlight'&&normal?normal.cloneNode(true):node.ownerDocument.createElementNS(NS,'Pair');field(pair,'key',state);map.append(pair);
 }
 return map;
}
function finish(model){const source=utf8.encode(new XMLSerializer().serializeToString(model.xml));parseKml(source);return source;}
function rgba(color,opacity){return Math.round(opacity*255).toString(16).padStart(2,'0')+color.slice(5,7)+color.slice(3,5)+color.slice(1,3);}
function defaults(view,kind){if(kind==='balloon')return {};if(kind==='line')return {color:rgba(view.line.color,view.line.opacity),width:String(view.line.weight)};if(kind==='poly')return {color:rgba(view.polygon.fillColor,view.polygon.fillOpacity),fill:view.polygon.fill,outline:view.polygon.stroke};if(kind==='icon')return {color:rgba(view.point.color,view.point.opacity),scale:'1'};return {color:rgba(view.label.color,view.label.opacity),scale:String(view.label.scale)};}
export function editKmlFeatureStyle(source,index,state,changes){
 if(!['normal','highlight','both'].includes(state))fail('selection');const model=parseKml(source),node=feature(model,index);if(!Object.keys(changes??{}).length)return model.source;
 const resolve=createKmlStyleResolver(model),states=state==='both'?['normal','highlight']:[state],snapshots=new Map(states.map(key=>[key,resolve(index,{highlight:key==='highlight'})])),map=mapFor(node);
 for(const key of states){const pair=children(map,'Pair').find(pair=>raw(pair,'key')===key),style=unique(pair,'Style')??pair.appendChild(model.xml.createElementNS(NS,'Style')),view=snapshots.get(key),seeded=Object.fromEntries(Object.entries(changes).map(([kind,fields])=>[kind,view.sourceStyle[kind]?fields:{...defaults(view,kind),...fields}]));patch(style,seeded);}
 return finish(model);
}
export function sharedKmlStyles(model){const count=new Map();for(const node of model.xml.getElementsByTagName('*'))if(node.hasAttribute('id'))count.set(node.getAttribute('id'),(count.get(node.getAttribute('id'))??0)+1);return [...model.xml.getElementsByTagNameNS(NS,'Style')].filter(node=>{if(!node.getAttribute('id'))return false;for(let parent=node;parent;parent=parent.parentElement)if(parent.namespaceURI===NS&&parent.localName==='Update')return false;return true;}).map(node=>({id:node.getAttribute('id'),ambiguous:count.get(node.getAttribute('id'))!==1,fields:kmlStyleFields(node)}));}
export function editSharedKmlStyle(source,id,changes){const model=parseKml(source),nodes=[...model.xml.getElementsByTagName('*')].filter(n=>n.getAttribute('id')===id);if(nodes.length!==1||nodes[0].namespaceURI!==NS||nodes[0].localName!=='Style')fail('ambiguous');if(!Object.keys(changes??{}).length)return model.source;patch(nodes[0],changes);return finish(model);}
export function resetKmlFeatureStyles(source,index){const model=parseKml(source),node=feature(model,index),selectors=[...node.children].filter(n=>n.namespaceURI===NS&&['Style','StyleMap'].includes(n.localName));if(selectors.some(hasId))fail('referenced');selectors.forEach(n=>n.remove());return selectors.length?finish(model):model.source;}
export function kmlFeatureHasInlineStyle(model,index){return [...feature(model,index).children].some(n=>n.namespaceURI===NS&&['Style','StyleMap'].includes(n.localName));}
export function createSharedKmlStyle(source,id,changes={}){const model=parseKml(source),style=appendSharedKmlSelector(model,'Style',id);patch(style,changes);return finish(model);}
export function kmlStyleMapStateView(model,id,state){
 if(!['normal','highlight'].includes(state))fail('selection');getSharedKmlStyleMap(model,id);const xml=model.xml.cloneNode(true),node=xml.createElementNS(NS,'Placemark'),reference=xml.createElementNS(NS,'styleUrl');reference.textContent='#'+encodeURIComponent(id);node.appendChild(reference);xml.documentElement.appendChild(node);const temporary={xml};return createKmlStyleResolver(temporary,{locale:'en'})(features(temporary).length-1,{highlight:state==='highlight'});
}
function statePair(map,state){const pairs=children(map,'Pair').filter(pair=>raw(pair,'key')===state);if(pairs.length>1||pairs.some(pair=>children(pair,'key').length!==1))fail('ambiguous');return pairs[0];}
function materializePair(map,state){
 const present=statePair(map,state);if(present)return present;const normal=state==='highlight'?statePair(map,'normal'):null,pair=normal?normal.cloneNode(true):map.ownerDocument.createElementNS(NS,'Pair');
 // Preserve copied branch identities with fresh IDs and remap internal links.
 // Merely stripping IDs would leave a copied reference pointing at the edited
 // original branch, so normal-only edits could still change highlight.
 if(normal){const count=new Map();for(const node of map.ownerDocument.getElementsByTagName('*'))if(node.hasAttribute('id'))count.set(node.getAttribute('id'),(count.get(node.getAttribute('id'))??0)+1);const remap=new Map();for(const node of [pair,...pair.getElementsByTagName('*')])if(node.hasAttribute('id')){const old=node.getAttribute('id');if(count.get(old)!==1)fail('ambiguous');let id;do{id='style-copy-'+crypto.randomUUID();}while(count.has(id));count.set(id,1);remap.set(old,id);node.setAttribute('id',id);}for(const node of [pair,...pair.getElementsByTagName('*')]){if(node.namespaceURI===NS&&node.localName==='styleUrl'){const url=node.textContent.trim();if(url.startsWith('#')){let id;try{id=decodeURIComponent(url.slice(1));}catch{}if(remap.has(id))node.textContent='#'+encodeURIComponent(remap.get(id));}}if(node.namespaceURI===NS&&remap.has(node.getAttribute('targetId')))node.setAttribute('targetId',remap.get(node.getAttribute('targetId')));}}
 field(pair,'key',state);if(!normal)pair.appendChild(map.ownerDocument.createElementNS(NS,'Style'));map.appendChild(pair);return pair;
}
function inlineOwner(map,state,seen=new Set()){
 if(seen.has(map)||seen.size>=16)fail('ambiguous');const next=new Set(seen);next.add(map);materializePair(map,'highlight');const pair=materializePair(map,state),styles=children(pair,'Style'),maps=children(pair,'StyleMap');
 if(styles.length+maps.length>1)fail('ambiguous');unique(pair,'styleUrl');if(maps.length)return inlineOwner(maps[0],state,next);if(styles.length)return styles[0];const style=map.ownerDocument.createElementNS(NS,'Style'),before=[...pair.children].find(node=>node.namespaceURI!==NS||!['key','styleUrl'].includes(node.localName));pair.insertBefore(style,before??null);return style;
}
export function editKmlStyleMapInline(source,id,state,changes){
 if(!['normal','highlight','both'].includes(state))fail('selection');const model=parseKml(source),map=getSharedKmlStyleMap(model,id);if(!changes||typeof changes!=='object'||Array.isArray(changes)||Object.entries(changes).some(([kind,fields])=>!tags[kind]||!fields||typeof fields!=='object'||Array.isArray(fields)||Object.keys(fields).some(key=>!allowed[kind].includes(key))))fail('selection');changes=Object.fromEntries(Object.entries(changes).filter(([,fields])=>Object.keys(fields).length));if(!Object.keys(changes).length)return model.source;const states=state==='both'?['normal','highlight']:[state],views=new Map(states.map(key=>[key,kmlStyleMapStateView(model,id,key)]));
 for(const key of states){const owner=inlineOwner(map,key),view=views.get(key),seeded=Object.fromEntries(Object.entries(changes).map(([kind,fields])=>[kind,view.sourceStyle[kind]?fields:{...defaults(view,kind),...fields}]));patch(owner,seeded);}return finish(model);
}
export function kmlStyleMapInlineIds(model,id,state){const map=getSharedKmlStyleMap(model,id),states=state==='both'?['normal','highlight']:[state],owned=new Set(),ids=new Set(),referenced=new Set();for(const key of states){const pair=statePair(map,key)??(key==='highlight'?statePair(map,'normal'):null);if(!pair)continue;for(const node of [pair,...pair.getElementsByTagName('*')])owned.add(node);for(const node of [...pair.children].filter(child=>['Style','StyleMap'].includes(child.localName)&&child.namespaceURI===NS))for(const child of [node,...node.getElementsByTagName('*')])if(child.getAttribute('id'))ids.add(child.getAttribute('id'));}for(const node of model.xml.getElementsByTagName('*'))if(!owned.has(node)){const url=node.namespaceURI===NS&&node.localName==='styleUrl'?node.textContent.trim():'';if(url.startsWith('#')){let target;try{target=decodeURIComponent(url.slice(1));}catch{}if(ids.has(target))referenced.add(target);}if(ids.has(node.getAttribute('targetId')))referenced.add(node.getAttribute('targetId'));}return [...referenced];}
