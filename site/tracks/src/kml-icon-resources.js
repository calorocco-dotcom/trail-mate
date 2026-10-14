import {useEffect,useMemo,useState} from 'react';
import {readTrackDraft} from './draft-store.js';
import {attachmentImageInfo} from './attachment-image-info.js';
import {attachmentDigest,attachmentHex} from './draft-attachments.js';
import {createKmlStyleResolver} from './kml-style.js';
import {collectGroundOverlays} from './kml-ground-overlays.js';
import {collectScreenOverlays} from './kml-screen-overlay.js';
import {collectPhotoOverlays} from './kml-photo-preview.js';
const EMPTY=new Map();
const EMPTY_REFERENCES=Object.freeze([]);
// Only exact, locally held attachment references. A URL in KML is never fetched.
// The shared reader has already verified the signed index; check actual bytes
// again before making a raster URL, including a bound on total decoded pixels.
export function useKmlIconResources({draft,packet,model,groundOverlays,screenOverlays,extraReferences=EMPTY_REFERENCES}={}){
 const references=useMemo(()=>{if(!model)return [];const resolve=createKmlStyleResolver(model),resolvePhoto=createKmlStyleResolver(model,{featureType:'PhotoOverlay'}),photos=collectPhotoOverlays(model).overlays,overlays=groundOverlays??collectGroundOverlays(model).overlays,screens=screenOverlays??collectScreenOverlays(model).overlays;return [...new Set([...model.features.flatMap((feature,index)=>[resolve(index).iconHref,resolve(index,{highlight:true}).iconHref]),...photos.filter(row=>row.visible).flatMap(row=>[resolvePhoto(row.index).iconHref,resolvePhoto(row.index,{highlight:true}).iconHref]),...overlays.filter(overlay=>overlay.visible).map(overlay=>overlay.href),...screens.map(overlay=>overlay.href),...extraReferences].filter(Boolean))].sort();},[model,groundOverlays,screenOverlays,extraReferences]);
 const referenceKey=JSON.stringify(references);
 const key=packet??(draft?draft.id+':'+draft.version:null),[state,setState]=useState({key:null,resources:EMPTY});
 useEffect(()=>{
  let active=true;const urls=[];
  async function load(){
   let rows;
   if(packet)rows=(packet.rows??[]).filter(row=>row.verified).map(row=>({...row,content:packet.files.find(file=>file.id===row.id)?.content}));
   else if(draft){const snapshot=await readTrackDraft(draft.id);if(snapshot.version!==draft.version)throw Error('Draft changed');rows=snapshot.attachmentRows;}
   else rows=[];
   const resources=new Map(),images=new Map();let pixels=0;
   for(const row of rows.slice(0,64)){
    if(!references.includes(row.reference))continue;
    if(!active)return;if(!(row.content instanceof Uint8Array)||row.content.length>4194304)continue;
    const content=row.content.slice(),info=attachmentImageInfo(content);if(!info)continue;
    const expected=typeof row.hash==='string'?row.hash:row.hash instanceof Uint8Array?attachmentHex(row.hash):null;
    const digest=attachmentHex(await attachmentDigest(content));if(!active)return;if(!expected||digest!==expected)continue;
    let image=images.get(digest);
    if(!image){if(pixels+info.width*info.height>8000000)continue;
     const blob=new Blob([content],{type:info.mime});let bitmap;try{bitmap=await createImageBitmap(blob);}catch{continue;}
     const valid=bitmap.width===info.width&&bitmap.height===info.height;bitmap.close();if(!active)return;if(!valid)continue;pixels+=info.width*info.height;
     const url=URL.createObjectURL(blob);urls.push(url);image={...info,url};images.set(digest,image);}
    if(typeof row.reference==='string')resources.set(row.reference,image);
   }
   if(active){Object.defineProperties(resources,{complete:{value:true},requested:{value:references.slice()}});setState({key,referenceKey,resources});}
  }
  load().catch(()=>{if(active){const resources=new Map();Object.defineProperties(resources,{complete:{value:true},requested:{value:references.slice()}});setState({key,referenceKey,resources});}});
  return()=>{active=false;for(const url of urls)URL.revokeObjectURL(url);};
 },[key,referenceKey]);
 return state.key===key&&state.referenceKey===referenceKey?state.resources:EMPTY;
}
