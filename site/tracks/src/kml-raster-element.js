export function kmlRasterElement({url,width,height,color='#ffffff',opacity=1,crop,imageSize}){
 const image=document.createElement('img');image.src=url;image.alt='';image.width=width;image.height=height;image.draggable=false;image.style.cssText='display:block;max-width:none;';image.style.opacity=String(opacity);
 const content=document.createElement('div');let raster=image;
 if(crop&&imageSize&&(crop.x!==0||crop.top!==0||crop.width!==imageSize[0]||crop.height!==imageSize[1])){
  raster=document.createElement('div');raster.className='kml-raster-crop';raster.style.cssText='display:block;position:relative;overflow:hidden;width:'+width+'px;height:'+height+'px;';
  const sx=width/crop.width,sy=height/crop.height;image.style.position='absolute';image.style.width=imageSize[0]*sx+'px';image.style.height=imageSize[1]*sy+'px';image.style.left=-crop.x*sx+'px';image.style.top=-crop.top*sy+'px';raster.append(image);
 }
 content.append(raster);
 if(/^#[a-f0-9]{6}$/i.test(color)&&color.toLowerCase()!=='#ffffff'){
  const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg'),defs=document.createElementNS(ns,'defs'),filter=document.createElementNS(ns,'filter'),matrix=document.createElementNS(ns,'feColorMatrix'),id='kml-raster-'+crypto.randomUUID();
  svg.setAttribute('aria-hidden','true');svg.setAttribute('width','0');svg.setAttribute('height','0');svg.style.position='absolute';filter.setAttribute('id',id);filter.setAttribute('color-interpolation-filters','sRGB');
  const [r,g,b]=color.match(/[a-f0-9]{2}/gi).map(part=>parseInt(part,16)/255);matrix.setAttribute('type','matrix');matrix.setAttribute('values',r+' 0 0 0 0 0 '+g+' 0 0 0 0 0 '+b+' 0 0 0 0 0 1 0');filter.append(matrix);defs.append(filter);svg.append(defs);content.append(svg);image.style.filter='url("#'+id+'")';
 }
 return {content,image,raster};
}
