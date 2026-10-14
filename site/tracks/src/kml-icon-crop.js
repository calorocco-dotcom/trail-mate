const GX='http://www.google.com/kml/ext/2.2';
const fail=()=>{throw Object.assign(Error('Invalid KML icon crop'),{code:'KML_ICON_CROP'});};
export function readKmlIconCrop(icon){
 if(!icon)return null;const values={x:0,y:0,w:-1,h:-1};let present=false;
 for(const key of Object.keys(values)){const matches=[...icon.children].filter(node=>node.namespaceURI===GX&&node.localName===key);if(matches.length>1)fail();if(!matches.length)continue;present=true;const raw=matches[0].textContent.trim();if(!/^[+-]?\d+$/.test(raw))fail();values[key]=Number(raw);}
 if(!present)return null;validateKmlIconCrop(values);return values;
}
export function validateKmlIconCrop(crop){
 if(!crop)return;for(const key of ['x','y','w','h']){const n=crop[key];if(!Number.isSafeInteger(n)||n>2147483647||(key==='x'||key==='y'?n<0:n!==-1&&n<=0))fail();}
}
export function resolveKmlIconCrop(crop,image){
 validateKmlIconCrop(crop);if(!Number.isSafeInteger(image.width)||!Number.isSafeInteger(image.height)||image.width<=0||image.height<=0)fail();
 const {x=0,y=0,w=-1,h=-1}=crop??{},width=w===-1?image.width:w,height=h===-1?image.height:h;
 if(x+width>image.width||y+height>image.height)fail();return {x,y,width,height,top:image.height-y-height};
}
