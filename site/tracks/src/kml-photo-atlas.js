// Only verified local resource URLs enter this bounded staging atlas. Its
// lifetime belongs to the viewer, not to the KML or attachment store.
export async function createPhotoAtlas(plan,resources){
 if(!plan?.visible||plan.tiles.length>16||plan.width*plan.height>4000000)throw Error('Invalid atlas budget');const canvas=document.createElement('canvas');canvas.width=plan.width;canvas.height=plan.height;const context=canvas.getContext('2d');
 try{for(const tile of plan.tiles){const image=resources.get(tile.reference);if(!image?.url?.startsWith('blob:')||image.width!==plan.tileSize||image.height!==plan.tileSize)throw Error('Pyramid tile unavailable');const raster=new Image();raster.src=image.url;await raster.decode();if(raster.naturalWidth!==plan.tileSize||raster.naturalHeight!==plan.tileSize)throw Error('Pyramid tile changed');context.drawImage(raster,tile.left,tile.top);}return {canvas,width:plan.width,height:plan.height,rect:plan.rect,close(){canvas.width=canvas.height=0;}};}catch(error){canvas.width=canvas.height=0;throw error;}
}
