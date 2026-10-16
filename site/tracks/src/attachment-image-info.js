// Read dimensions before creating a browser image, avoiding decompression of
// unbounded raster sizes. Signature/hash checks belong to the package reader.
export function attachmentImageInfo(content){
 if(!(content instanceof Uint8Array))return null;
 let width,height,mime;
 if(content.length>=33&&[137,80,78,71,13,10,26,10].every((value,i)=>content[i]===value)&&
  content[8]===0&&content[9]===0&&content[10]===0&&content[11]===13&&String.fromCharCode(...content.slice(12,16))==='IHDR'){
  const view=new DataView(content.buffer,content.byteOffset,content.byteLength);width=view.getUint32(16);height=view.getUint32(20);mime='image/png';
 }else if(content.length>=4&&content[0]===255&&content[1]===216){
  let offset=2;
  while(offset<content.length){
   if(content[offset++]!==255)return null;while(content[offset]===255)offset++;const marker=content[offset++];
   if(marker===218||marker===217)break;
   if(marker===1||marker>=208&&marker<=215)continue;
   if(offset+2>content.length)return null;const size=content[offset]*256+content[offset+1];if(size<2||offset+size>content.length)return null;
   if([192,193,194,195,197,198,199,201,202,203,205,206,207].includes(marker)){
    if(width!==undefined||size<8)return null;height=content[offset+3]*256+content[offset+4];width=content[offset+5]*256+content[offset+6];mime='image/jpeg';
   }
   offset+=size;
  }
 }
 return width>0&&height>0&&width<=8192&&height<=8192&&width*height<=32000000?{width,height,mime}:null;
}
