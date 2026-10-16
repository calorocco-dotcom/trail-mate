import {parseKml} from './kml.js';
import {validTrackDateTime} from './track-datetime.js';
const NS='http://www.opengis.net/kml/2.2',GX='http://www.google.com/kml/ext/2.2',utf8=new TextEncoder();
const featureTypes=new Set(['Document','Folder','Placemark','GroundOverlay','NetworkLink','ScreenOverlay','PhotoOverlay']);
const baseExtensions=new Set(['ObjectSimpleExtensionGroup','AbstractTimePrimitiveSimpleExtensionGroup','AbstractTimePrimitiveObjectExtensionGroup']);
// Feature time precedes styles and all concrete-feature content. Document and
// Folder children, and Screen/PhotoOverlay fields, must obey the same order.
const afterTime=new Set(['styleUrl','Style','StyleMap','Region','ExtendedData','Schema',...featureTypes,'color','drawOrder','Icon','Link','Url','refreshVisibility','flyToView','altitude','altitudeMode','LatLonBox','overlayXY','screenXY','rotationXY','size','rotation','ViewVolume','ImagePyramid','shape','Point','LineString','LinearRing','Polygon','MultiGeometry','Model']);
const children=(node,name)=>[...(node?.children??[])].filter(child=>child.namespaceURI===NS&&child.localName===name);
const fail=reason=>{throw Object.assign(Error(reason),{code:'KML_TIME_EDIT_'+reason.toUpperCase()});};
const raw=(node,name)=>children(node,name)[0]?.textContent??'';
// Order comparison is independent of JS Date limits and retains fractional
// precision. Floating times use the same explicit UTC comparison policy as UI.
export function kmlTimeEditDate(input){
 if(typeof input!=='string'||input.length>63)fail('invalid');
 const text=input.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g,''),match=/^(-?\d{4,})(?:-(\d{2})(?:-(\d{2})(?:T(\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?)?)?)?(Z|[+-]\d{2}:\d{2})?$/.exec(text);
 if(!match)fail('invalid');
 const [,yearText,monthText,dayText,hourText,minuteText,secondText,fraction='',zone='']=match;
 if(!validTrackDateTime(`${yearText}-${monthText??'01'}-${dayText??'01'}T${hourText??'00'}:${minuteText??'00'}:${secondText??'00'}${fraction?'.'+fraction:''}${zone}`))fail('invalid');
 const absolute=BigInt(yearText.startsWith('-')?yearText.slice(1):yearText),year=yearText.startsWith('-')?1n-absolute:absolute,previous=year-1n;
 const floor=(value,divisor)=>value>=0n?value/divisor:(value-divisor+1n)/divisor;
 const leap=year%4n===0n&&(year%100n!==0n||year%400n===0n),month=Number(monthText??1),before=[0,31,59,90,120,151,181,212,243,273,304,334][month-1]+(leap&&month>2?1:0);
 const days=365n*previous+floor(previous,4n)-floor(previous,100n)+floor(previous,400n)+BigInt(before+Number(dayText??1)-1);
 const offset=!zone||zone==='Z'?0:(zone[0]==='-'?-1:1)*(Number(zone.slice(1,3))*60+Number(zone.slice(4)));
 return {seconds:days*86400n+BigInt(Number(hourText??0)*3600+Number(minuteText??0)*60+Number(secondText??0)-offset*60),fraction};
}
export function compareKmlTimeEditDates(left,right){const a=kmlTimeEditDate(left),b=kmlTimeEditDate(right);if(a.seconds!==b.seconds)return a.seconds<b.seconds?-1:1;const width=Math.max(a.fraction.length,b.fraction.length),x=a.fraction.padEnd(width,'0'),y=b.fraction.padEnd(width,'0');return x===y?0:x<y?-1:1;}
export function validateKmlTimePatch(patch){
 if(!patch||typeof patch!=='object'||Array.isArray(patch)||Object.keys(patch).some(key=>!['mode','when','begin','end'].includes(key))||!['inherit','stamp','span'].includes(patch.mode))fail('invalid');
 if(patch.mode==='stamp')kmlTimeEditDate(patch.when);
 if(patch.mode==='span'){if(typeof patch.begin!=='string'||typeof patch.end!=='string'||!patch.begin&&!patch.end)fail('invalid');if(patch.begin)kmlTimeEditDate(patch.begin);if(patch.end)kmlTimeEditDate(patch.end);if(patch.begin&&patch.end&&compareKmlTimeEditDates(patch.begin,patch.end)>=0)fail('invalid');}
 return patch;
}
export function activeKmlTimeValues(values){return values.mode==='stamp'?{mode:'stamp',when:values.when}:values.mode==='span'?{mode:'span',begin:values.begin,end:values.end}:{mode:values.mode};}
function ownTime(node){
 const primitives=[...children(node,'TimeStamp'),...children(node,'TimeSpan')];
 const primitive=primitives[0],mode=primitive?.localName==='TimeStamp'?'stamp':primitive?'span':'inherit';
 const ambiguous=primitives.length>1||primitive&&['when','begin','end'].some(name=>children(primitive,name).length>1||children(primitive,name).some(child=>child.children.length>0));
 return {primitive,ambiguous:Boolean(ambiguous),values:{mode,when:raw(primitive,'when'),begin:raw(primitive,'begin'),end:raw(primitive,'end')}};
}
function targets(model){
 const rows=[];let featureIndex=0;
 const walk=(node,path,inherited)=>{
  if(!(node.namespaceURI===NS&&featureTypes.has(node.localName)||node.namespaceURI===GX&&node.localName==='Tour'))return;
  const name=raw(node,'name'),id=node.getAttribute('id'),local=ownTime(node),type=node.namespaceURI===GX?'gx:Tour':node.localName;
  const row={index:rows.length,type,name,id,path,featureIndex:node.localName==='Placemark'?featureIndex++:null,local:local.values,ambiguous:local.ambiguous,inherited,node,primitive:local.primitive};rows.push(row);
  if(['Document','Folder'].includes(type)){const next=local.values.mode==='inherit'?inherited:{...local.values,owner:name||id||type,ambiguous:local.ambiguous};for(const child of node.children)walk(child,[...path,name||id||type],next);}
 };
 for(const node of model.xml.documentElement.children)walk(node,[],null);return rows;
}
export function kmlTimeEditors(model){return targets(model).map(({node,primitive,...record})=>record);}
function target(model,index){const row=targets(model)[index];if(!Number.isInteger(index)||index<0||!row)fail('selection');if(row.ambiguous)fail('ambiguous');return row;}
function lossDetails(node){
 if(!node)return [];const details=[...node.attributes].map(attribute=>node.tagName+' · @'+attribute.name);
 for(const child of node.childNodes)if(child.nodeType===1)details.push(child.tagName);else if(child.nodeType===8)details.push(node.tagName+' · comment');return details;
}
function plan(model,row,patch){
 const old=row.primitive,nextType=patch.mode==='stamp'?'TimeStamp':patch.mode==='span'?'TimeSpan':null,conversion=old&&nextType&&old.localName!==nextType,removed=[];
 if(!old)return {loss:[],referenced:false};
 if(!nextType)removed.push(old);
 else if(conversion){for(const child of old.children)if(!(child.namespaceURI===NS&&baseExtensions.has(child.localName)))removed.push(child);}
 else{for(const field of old.localName==='TimeStamp'?['when']:['begin','end'])if(patch[field]==='')removed.push(...children(old,field));}
 const ids=new Set();for(const node of removed)for(const owned of [node,...node.getElementsByTagName('*')])if(owned.getAttribute('id'))ids.add(owned.getAttribute('id'));
 if(conversion&&old.getAttribute('id'))ids.add(old.getAttribute('id'));
 const referenced=[...model.xml.getElementsByTagName('*')].some(node=>!old.contains(node)&&ids.has(node.getAttribute('targetId')));
 const loss=[];
 if(!nextType){loss.push(...[...old.attributes].map(attribute=>old.tagName+' · @'+attribute.name));for(const child of old.children)if(!['when','begin','end'].includes(child.localName)||child.namespaceURI!==NS)loss.push(child.tagName);else loss.push(...lossDetails(child));for(const child of old.childNodes)if(child.nodeType===8)loss.push(old.tagName+' · comment');}
 else for(const node of removed){if(node.namespaceURI===NS&&['when','begin','end'].includes(node.localName))loss.push(...lossDetails(node));else loss.push(node.tagName);}
 return {loss:[...new Set(loss)],referenced};
}
export function kmlTimeChangePlan(model,index,patch){validateKmlTimePatch(patch);return plan(model,target(model,index),patch);}
function scalar(parent,name,value){
 let node=children(parent,name)[0];if(value===''){node?.remove();return;}
 if(!node){node=parent.ownerDocument.createElementNS(NS,parent.prefix?parent.prefix+':'+name:name);const after=[...parent.children].find(child=>child.namespaceURI!==NS||!baseExtensions.has(child.localName)&&!['when','begin'].includes(child.localName));parent.insertBefore(node,after??null);}
 const content=[...node.childNodes].filter(child=>child.nodeType===3||child.nodeType===4);if(content.length){content[0].nodeValue=value;for(const child of content.slice(1))child.nodeValue='';}else node.insertBefore(parent.ownerDocument.createTextNode(value),node.firstChild);
}
export function editKmlTime(source,index,patch,{allowLoss=false}={}){
 validateKmlTimePatch(patch);const model=parseKml(source),row=target(model,index),old=row.primitive;
 if(JSON.stringify(activeKmlTimeValues(patch))===JSON.stringify(activeKmlTimeValues(row.local)))return model.source;
 const changes=plan(model,row,patch);if(changes.referenced)fail('referenced');if(changes.loss.length&&allowLoss!==true)fail('loss');
 if(patch.mode==='inherit')old?.remove();
 else{
  const type=patch.mode==='stamp'?'TimeStamp':'TimeSpan';let primitive=old;
  if(!old||old.localName!==type){const prefix=old?.prefix??row.node.lookupPrefix(NS);primitive=model.xml.createElementNS(NS,prefix?prefix+':'+type:type);
   if(old){for(const attribute of old.attributes)primitive.setAttributeNS(attribute.namespaceURI,attribute.name,attribute.value);for(const child of old.childNodes)if(child.nodeType===8||child.nodeType===3||child.nodeType===4||child.nodeType===1&&child.namespaceURI===NS&&baseExtensions.has(child.localName))primitive.append(child.cloneNode(true));old.replaceWith(primitive);}
   else{const after=[...row.node.children].find(child=>child.namespaceURI===NS&&afterTime.has(child.localName)||child.namespaceURI===GX&&['Track','MultiTrack','Tour','Playlist','altitudeMode','LatLonQuad','drawOrder'].includes(child.localName));row.node.insertBefore(primitive,after??null);}
  }
  for(const field of type==='TimeStamp'?['when']:['begin','end'])scalar(primitive,field,patch[field]);
 }
 const result=utf8.encode(new XMLSerializer().serializeToString(model.xml));parseKml(result);return result;
}
