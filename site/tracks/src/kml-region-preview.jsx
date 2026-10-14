import React,{useEffect,useMemo,useState} from 'react';import L from 'leaflet';
import {collectKmlRegionScopes,kmlRegionOpacity} from './kml-region.js';import {kmlRegionText} from '../../i18n/kml-region-labels.js';import {useManagementLocale} from '../../i18n/management-copy.jsx';
// Call after the component's map-initialization effect, so registration sees it.
export function useKmlRegionPreview(model,overlays,map){
 const scopes=useMemo(()=>collectKmlRegionScopes(model,overlays),[model,overlays]),[viewport,setViewport]=useState(null),[choice,setChoice]=useState({model,enabled:true});
 const hasRegions=Boolean(scopes.declared.length);
 useEffect(()=>{const view=map.current;if(!view)return;if(!hasRegions){setViewport(null);return;}let closed=false;const update=()=>{if(closed)return;const bounds=view.getBounds();setViewport({zoom:view.getZoom(),bounds:{west:bounds.getWest(),east:bounds.getEast(),north:bounds.getNorth(),south:bounds.getSouth()}});};view.on('moveend zoomend resize',update);update();return()=>{closed=true;view.off('moveend zoomend resize',update);};},[map,hasRegions]);
 useEffect(()=>{setChoice(previous=>previous.model===model?previous:{model,enabled:true});},[model]);
 const enabled=choice.model===model?choice.enabled:true,opacities=useMemo(()=>scopes.features.map(region=>enabled?kmlRegionOpacity(region,viewport):1),[scopes,viewport,enabled]);
 return {scopes,viewport,enabled,opacities,overlayOpacity:index=>enabled?kmlRegionOpacity(scopes.overlays.get(index),viewport):1,onEnabled:value=>setChoice({model,enabled:value})};
}
export function KmlRegionPreview({scopes,enabled,onEnabled}){const locale=useManagementLocale(),text=kmlRegionText(locale),number=new Intl.NumberFormat(locale);if(!scopes.declared.length)return null;return <section className="tracks-region-preview"><h3>{text.title}</h3><p className="tracks-help">{text.help}</p><label><input type="checkbox" checked={enabled} onChange={event=>onEnabled(event.target.checked)}/>{text.enabled}</label>{['invalid','altitude','projection','overlap'].filter(key=>scopes[key]).map(key=><p className="tracks-help" key={key}>{text[key].replace('{count}',number.format(scopes[key]))}</p>)}</section>;}
export function fitKmlRegionSource(view,model,summary,overlays,fitted){
 if(!model||fitted.current===model)return;const bounds=L.latLngBounds([]),box=summary?.bounds,reference=box?(box.west+box.east+(box.crossesDateLine?360:0))/2:overlays.overlays[0]?.corners[0]?.longitude??0;
 if(box)bounds.extend([[box.south,box.west],[box.north,box.east+(box.crossesDateLine?360:0)]]);for(const overlay of overlays.overlays)if(overlay.visible)for(const point of overlay.corners)bounds.extend([point.latitude,point.longitude+360*Math.round((reference-point.longitude)/360)]);
 if(bounds.isValid()){view.fitBounds(bounds,{padding:[30,30],maxZoom:16});fitted.current=model;}
}
