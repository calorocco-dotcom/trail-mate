import L from 'leaflet';
import {screenOverlayLayout} from './kml-screen-overlay.js';
import {kmlRasterElement} from './kml-raster-element.js';
import {resolveKmlIconCrop} from './kml-icon-crop.js';
export class KmlScreenOverlayLayer extends L.Layer{
 constructor(overlay,image,rank=0){super();this.overlay=overlay;this.image=image;this.rank=rank;}
 // FeatureGroup asks every child for bounds. A screen layer must not expand
 // geographic fitBounds or invent a latitude/longitude for a viewport anchor.
 getBounds(){return L.latLngBounds([]);}
 onAdd(map){let pane=map.getContainer().querySelector(':scope > .kml-screen-overlay-pane');if(!pane){pane=document.createElement('div');pane.className='kml-screen-overlay-pane';pane.style.cssText='position:absolute;inset:0;z-index:450;pointer-events:none;overflow:hidden;';map.getContainer().append(pane);}this.pane=pane;this.element=document.createElement('div');this.element.className='kml-screen-overlay';this.element.style.cssText='position:absolute;pointer-events:none;transform-origin:0 0;z-index:'+this.rank+';';this.element.dataset.overlayId=this.overlay.id??String(this.overlay.index);this.element.setAttribute('aria-label',this.overlay.name||'ScreenOverlay');pane.append(this.element);map.on('resize',this.update,this);this.update();}
 update(){if(!this._map||!this.element)return;try{const size=this._map.getSize(),layout=screenOverlayLayout(this.overlay,{width:size.x,height:size.y},this.image);this.element.replaceChildren();this.element.style.display='';this.element.style.width=layout.width+'px';this.element.style.height=layout.height+'px';this.element.style.transform=`translate(${layout.x}px,${layout.y}px) rotate(${layout.rotation}deg)`;
  if(this.image){const{content}=kmlRasterElement({url:this.image.url,width:layout.width,height:layout.height,imageSize:[this.image.width,this.image.height],crop:this.image.crop,color:this.overlay.color,opacity:this.overlay.opacity});this.element.append(content);}else{this.element.style.background=this.overlay.color;this.element.style.opacity=String(this.overlay.opacity);}delete this.element.dataset.layoutError;
 }catch{this.element.style.display='none';this.element.dataset.layoutError='true';}}
 onRemove(map){map.off('resize',this.update,this);this.element?.remove();if(!this.pane?.children.length)this.pane?.remove();this.element=this.pane=null;}
}
export function addScreenOverlayLayers(group,parsed,resources){const issues=[];let rank=0;for(const overlay of parsed.overlays){let image=overlay.href?resources.get(overlay.href):null;if(overlay.href&&!image){issues.push({code:'missing',name:overlay.name});continue;}if(image){try{image={...image,crop:resolveKmlIconCrop(overlay.crop,image)};}catch{issues.push({code:'crop',name:overlay.name});continue;}}const layer=new KmlScreenOverlayLayer(overlay,image,rank++).addTo(group);if(layer.element?.dataset.layoutError)issues.push({code:'layout',name:overlay.name});}return issues;}
