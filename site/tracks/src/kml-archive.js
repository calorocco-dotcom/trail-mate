import {Inflate} from 'fflate';

// Browser-only unsigned archive reader. ZIP integrity is not author proof.
export const KML_ARCHIVE_LIMIT=Object.freeze({input:65*1024*1024,total:64*1024*1024,file:8*1024*1024,files:65,entries:256,name:4096});
const DRAFT_ARCHIVE_LIMIT=Object.freeze({...KML_ARCHIVE_LIMIT,total:65*1024*1024,files:72});
const fail=(code='KML_ARCHIVE_INVALID')=>{throw Object.assign(Error(code),{code});};
const check=signal=>{if(signal?.aborted)throw signal.reason??new DOMException('Aborted','AbortError');};
const utf8=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}),utf8Encoder=new TextEncoder();
const cp437=Array.from('ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ');
const crcTable=Uint32Array.from({length:256},(_,value)=>{for(let bit=0;bit<8;bit++)value=value&1?(value>>>1)^0xedb88320:value>>>1;return value>>>0;});
function crc(bytes,state=0xffffffff){for(const byte of bytes)state=crcTable[(state^byte)&255]^(state>>>8);return state>>>0;}
function filename(raw,flags,extra){
 let name;
 if(flags&0x800){try{name=utf8.decode(raw);}catch{fail();}}
 else name=Array.from(raw,byte=>byte<128?String.fromCharCode(byte):cp437[byte-128]).join('');
 const unicode=extra.get(0x7075);
 if(unicode){
  if(unicode.length<5||unicode[0]!==1)fail();
  if(new DataView(unicode.buffer,unicode.byteOffset,unicode.byteLength).getUint32(1,true)===(crc(raw)^0xffffffff)>>>0){
   let decoded;try{decoded=utf8.decode(unicode.subarray(5));}catch{fail();}
   if(flags&0x800&&name!==decoded)fail();name=decoded;
  }
 }
 if(!name||utf8Encoder.encode(name).length>KML_ARCHIVE_LIMIT.name||name.startsWith('/')||/[\\:\x00-\x1f\x7f]/.test(name))fail('KML_ARCHIVE_PATH');
 const parts=name.split('/');if(parts.at(-1)==='')parts.pop();
 if(!parts.length||parts.some(part=>!part||part==='.'||part==='..'))fail('KML_ARCHIVE_PATH');
 return name;
}

// PKWARE APPNOTE: ordinary/ZIP64 central directory, local headers and optional
// descriptors. Admit every span before feeding any compressed bytes to Inflate.
function directory(input,limits){
 if(!(input instanceof Uint8Array)||input.length<22||input.length>KML_ARCHIVE_LIMIT.input)fail('KML_ARCHIVE_LIMIT');
 const view=new DataView(input.buffer,input.byteOffset,input.byteLength);
 const fits=(offset,size)=>Number.isSafeInteger(offset)&&Number.isSafeInteger(size)&&offset>=0&&size>=0&&offset+size<=input.length;
 const u16=offset=>{if(!fits(offset,2))fail();return view.getUint16(offset,true);};
 const u32=offset=>{if(!fits(offset,4))fail();return view.getUint32(offset,true);};
 const u64=offset=>{if(!fits(offset,8))fail();const value=view.getBigUint64(offset,true);if(value>BigInt(Number.MAX_SAFE_INTEGER))fail('KML_ARCHIVE_LIMIT');return Number(value);};
 const extras=(offset,size)=>{
  if(!fits(offset,size))fail();const result=new Map(),end=offset+size;
  while(offset<end){if(offset+4>end)fail();const id=u16(offset),length=u16(offset+2);offset+=4;if(offset+length>end||result.has(id))fail();result.set(id,input.subarray(offset,offset+length));offset+=length;}
  return result;
 };
 let end=-1;
 for(let offset=input.length-22;offset>=Math.max(0,input.length-65557);offset--)if(u32(offset)===0x06054b50&&offset+22+u16(offset+20)===input.length){end=offset;break;}
 if(end<0||u16(end+4)!==0||u16(end+6)!==0)fail();
 let count=u16(end+10),size=u32(end+12),start=u32(end+16),boundary=end;
 if(u16(end+8)!==count)fail();
 if(count===0xffff||size===0xffffffff||start===0xffffffff){
  const locator=end-20;if(u32(locator)!==0x07064b50||u32(locator+4)!==0||u32(locator+16)!==1)fail();
  const record=u64(locator+8);if(u32(record)!==0x06064b50||u64(record+4)<44||record+12+u64(record+4)!==locator||u32(record+16)||u32(record+20)||u64(record+24)!==u64(record+32))fail();
  count=u64(record+32);size=u64(record+40);start=u64(record+48);boundary=record;
 }
 if(!count||count>KML_ARCHIVE_LIMIT.entries||start+size!==boundary||!fits(start,size))fail('KML_ARCHIVE_LIMIT');
 const rows=[],names=new Set(),ranges=[];let position=start,total=0,files=0;
 for(let index=0;index<count;index++){
  if(position+46>boundary||u32(position)!==0x02014b50)fail();
  const flags=u16(position+8),method=u16(position+10),checksum=u32(position+16),nameSize=u16(position+28),extraSize=u16(position+30),commentSize=u16(position+32);
  if(flags&~0x080e||![0,8].includes(method))fail('KML_ARCHIVE_UNSUPPORTED');
  const next=position+46+nameSize+extraSize+commentSize;if(next>boundary||!nameSize||nameSize>KML_ARCHIVE_LIMIT.name)fail();
  const raw=input.subarray(position+46,position+46+nameSize),extra=extras(position+46+nameSize,extraSize),name=filename(raw,flags,extra),key=name.replace(/\/$/,'');
  if(names.has(key))fail('KML_ARCHIVE_PATH');names.add(key);
  let compressed=u32(position+20),original=u32(position+24),local=u32(position+42),disk=u16(position+34),z=0;
  const zip64=extra.get(1),wide=offset=>{if(!zip64||offset+8>zip64.length)fail();const n=new DataView(zip64.buffer,zip64.byteOffset,zip64.byteLength).getBigUint64(offset,true);if(n>BigInt(Number.MAX_SAFE_INTEGER))fail('KML_ARCHIVE_LIMIT');return Number(n);};
  if(original===0xffffffff){original=wide(z);z+=8;}if(compressed===0xffffffff){compressed=wide(z);z+=8;}if(local===0xffffffff){local=wide(z);z+=8;}
  if(disk===0xffff){if(!zip64||z+4>zip64.length)fail();disk=new DataView(zip64.buffer,zip64.byteOffset,zip64.byteLength).getUint32(z,true);}
  if(disk!==0)fail('KML_ARCHIVE_UNSUPPORTED');
  const kind=(u32(position+38)>>>16)&0xf000,isDirectory=name.endsWith('/');
  if(kind&&kind!==(isDirectory?0x4000:0x8000))fail('KML_ARCHIVE_UNSUPPORTED');
  if(original>limits.file||(total+=original)>limits.total||!isDirectory&&++files>limits.files)fail('KML_ARCHIVE_LIMIT');
  if(isDirectory&&original!==0||method===0&&compressed!==original||compressed===0&&original!==0)fail();
  if(local+30>start||u32(local)!==0x04034b50||u16(local+6)!==flags||u16(local+8)!==method||u16(local+26)!==nameSize)fail();
  const localExtraSize=u16(local+28),data=local+30+nameSize+localExtraSize;
  if(data+compressed>start||!fits(data,compressed)||!raw.every((byte,i)=>byte===input[local+30+i]))fail();
  const localExtra=extras(local+30+nameSize,localExtraSize),local64=localExtra.get(1);let lz=0;
  const localWide=()=>{if(!local64||lz+8>local64.length)fail();const n=new DataView(local64.buffer,local64.byteOffset,local64.byteLength).getBigUint64(lz,true);lz+=8;if(n>BigInt(Number.MAX_SAFE_INTEGER))fail();return Number(n);};
  let localOriginal=u32(local+22),localCompressed=u32(local+18);if(localOriginal===0xffffffff)localOriginal=localWide();if(localCompressed===0xffffffff)localCompressed=localWide();
  const localCrc=u32(local+14);
  if(flags&8){if(localCrc!==0&&localCrc!==checksum||localOriginal!==0&&localOriginal!==original||localCompressed!==0&&localCompressed!==compressed)fail();}
  else if(localCrc!==checksum||localOriginal!==original||localCompressed!==compressed)fail();
  let finish=data+compressed;
  if(flags&8){
   const descriptor=offset=>offset+12<=start&&u32(offset)===checksum&&u32(offset+4)===compressed&&u32(offset+8)===original;
   const wideDescriptor=offset=>offset+20<=start&&u32(offset)===checksum&&view.getBigUint64(offset+4,true)===BigInt(compressed)&&view.getBigUint64(offset+12,true)===BigInt(original);
   if(local64&&finish+24<=start&&u32(finish)===0x08074b50&&wideDescriptor(finish+4))finish+=24;
   else if(local64&&wideDescriptor(finish))finish+=20;
   else if(finish+16<=start&&u32(finish)===0x08074b50&&descriptor(finish+4))finish+=16;
   else if(descriptor(finish))finish+=12;
   else if(finish+24<=start&&u32(finish)===0x08074b50&&wideDescriptor(finish+4))finish+=24;
   else if(wideDescriptor(finish))finish+=20;
   else fail();
  }
  ranges.push([local,finish]);rows.push({name,original,compressed,checksum,method,data,isDirectory});position=next;
 }
 if(position!==boundary)fail();
 ranges.sort((a,b)=>a[0]-b[0]);let previous=0;for(const [first,last] of ranges){if(first!==previous||last>start)fail();previous=last;}if(previous!==start)fail();
 // A file cannot also be a parent directory for another entry.
 const regular=new Set(rows.filter(row=>!row.isDirectory).map(row=>row.name));
 for(const row of rows){const parts=row.name.replace(/\/$/,'').split('/');parts.pop();while(parts.length){if(regular.has(parts.join('/')))fail('KML_ARCHIVE_PATH');parts.pop();}}
 if(!rows.some(row=>!row.isDirectory&&/\.kml$/i.test(row.name)))fail('KML_ARCHIVE_NO_KML');
 return rows;
}

export async function readKmlArchive(input,{signal,profile='kml'}={}){
 if(!['kml','draft'].includes(profile))fail();
 check(signal);const entries=directory(input,profile==='draft'?DRAFT_ARCHIVE_LIMIT:KML_ARCHIVE_LIMIT),files=new Map();let pushes=0;
 for(const row of entries){
  check(signal);const output=new Uint8Array(row.original);let length=0,checksum=0xffffffff;
  const receive=bytes=>{check(signal);if(length+bytes.length>row.original)fail();output.set(bytes,length);length+=bytes.length;checksum=crc(bytes,checksum);};
  try{
   if(row.method===0){for(let i=0;i<row.compressed;i+=32768){receive(input.subarray(row.data+i,row.data+Math.min(row.compressed,i+32768)));if(++pushes%32===0)await new Promise(resolve=>setTimeout(resolve,0));}}
   else{
    const stream=new Inflate(receive);
    if(row.compressed===0)stream.push(new Uint8Array(),true);
    for(let i=0;i<row.compressed;i+=1024){check(signal);const next=Math.min(row.compressed,i+1024);stream.push(input.subarray(row.data+i,row.data+next),next===row.compressed);if(++pushes%32===0)await new Promise(resolve=>setTimeout(resolve,0));}
   }
  }catch(error){if(signal?.aborted)check(signal);if(error.code?.startsWith?.('KML_ARCHIVE_'))throw error;fail();}
  if(length!==row.original||((checksum^0xffffffff)>>>0)!==row.checksum)fail();
  if(!row.isDirectory)files.set(row.name,output);
 }
 check(signal);const kmlFiles=[...files.keys()].filter(name=>/\.kml$/i.test(name));
 const roots=kmlFiles.filter(name=>name.toLowerCase()==='doc.kml');
 const preferred=kmlFiles.includes('doc.kml')?'doc.kml':roots.length===1?roots[0]:(kmlFiles.length===1?kmlFiles[0]:null);
 return {files,kmlFiles,preferred,totalBytes:entries.reduce((sum,row)=>sum+row.original,0)};
}

export function chooseArchiveKml(archive,name=archive?.preferred){
 if(!archive?.files||!Array.isArray(archive.kmlFiles)||!name||!archive.kmlFiles.includes(name))fail('KML_ARCHIVE_SELECT');
 const kml=archive.files.get(name);if(!(kml instanceof Uint8Array)||!kml.length||kml.length>KML_ARCHIVE_LIMIT.file)fail();
 return {name,kml:kml.slice(),files:archive.files};
}
