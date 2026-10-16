import L from 'leaflet';import {overlayHomography} from './kml-ground-overlays.js';import {kmlRasterElement} from './kml-raster-element.js';
import {resolveKmlIconCrop} from './kml-icon-crop.js';
export class KmlGroundOverlayLayer extends L.Layer{
 constructor(overlay,image,rank=0){super();this.overlay=overlay;this.image=image;this.rank=rank;}
 onAdd(map){
  const pane=map.getPane('kml-ground-overlays')??map.createPane('kml-ground-overlays');pane.style.zIndex='350';pane.style.pointerEvents='none';
  const value=this.overlay;let content,image;
  if(this.image){const crop=this.image.crop;({content,raster:image}=kmlRasterElement({url:this.image.url,width:crop.width,height:crop.height,imageSize:[this.image.width,this.image.height],crop,color:value.color,opacity:value.opacity}));this.width=crop.width;this.height=crop.height;}
  else{content=document.createElement('div');image=document.createElement('div');image.style.cssText='width:1px;height:1px;background:'+value.color+';opacity:'+value.opacity+';';content.append(image);this.width=this.height=1;}
  this.element=content;this.raster=image;content.className='kml-ground-overlay leaflet-zoom-hide';content.style.cssText='position:absolute;pointer-events:none;z-index:'+this.rank+';';content.dataset.overlayId=value.id??String(value.index);content.dataset.drawOrder=String(value.drawOrder);content.setAttribute('aria-label',value.name||'GroundOverlay');image.style.transformOrigin='0 0';pane.append(content);
  map.on('viewreset zoomend moveend resize',this.updateProjection,this);this.updateProjection();
 }
 onRemove(map){map.off('viewreset zoomend moveend resize',this.updateProjection,this);this.element?.remove();this.element=this.raster=null;}
 getCorners(){const points=this.overlay.corners,center=points.reduce((sum,p)=>sum+p.longitude,0)/4,reference=this._map?.getCenter().lng??center,shift=360*Math.round((reference-center)/360);return points.map(p=>L.latLng(p.latitude,p.longitude+shift));}
 getBounds(){return L.latLngBounds(this.getCorners());}
 updateProjection(){
  if(!this.element||!this._map)return;const corners=this.getCorners().map(point=>this._map.latLngToLayerPoint(point)),projected=overlayHomography(corners);if(!projected){this.element.style.display='none';return;}
  this.element.style.display='';this.element.style.left=projected.origin.x+'px';this.element.style.top=projected.origin.y+'px';const {h}=projected,w=this.width,t=this.height;
  this.raster.style.transform='matrix3d('+[h[0]/w,h[3]/w,0,h[6]/w,h[1]/t,h[4]/t,0,h[7]/t,0,0,1,0,h[2],h[5],0,1].join(',')+')';
 }
}
export function addGroundOverlayLayers(group,parsed,resources){
 const missing=[];let rank=0;for(const overlay of parsed.overlays){if(!overlay.visible)continue;let image=overlay.href?resources.get(overlay.href):null;if(overlay.href&&!image){missing.push({code:'missing',name:overlay.name});continue;}if(image){try{image={...image,crop:resolveKmlIconCrop(overlay.crop,image)};}catch{missing.push({code:'crop',name:overlay.name});continue;}}new KmlGroundOverlayLayer(overlay,image,rank++).addTo(group);}return missing;
}
