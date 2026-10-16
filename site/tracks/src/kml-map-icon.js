import L from 'leaflet';
import {kmlRasterElement} from './kml-raster-element.js';
// Rotation is on the child image, leaving Leaflet's marker translation intact.
export function kmlMapIcon(appearance){
 const value=appearance.icon;if(!value)return null;
 const {content,raster}=kmlRasterElement({url:value.url,width:value.size[0],height:value.size[1],color:value.color,opacity:value.opacity,crop:value.crop,imageSize:value.imageSize});
 raster.style.transformOrigin=value.anchor[0]+'px '+value.anchor[1]+'px';raster.style.transform='rotate('+value.heading+'deg)';
 return L.divIcon({html:content,className:'kml-map-icon',iconSize:value.size,iconAnchor:value.anchor});
}
