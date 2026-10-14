import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackPackageText} from '../../i18n/track-package-labels.js';
import {attachmentImageInfo} from './attachment-image-info.js';
import {PackageArchiveExport} from './package-archive-export.jsx';
import {trackStorageErrorText} from '../../i18n/track-storage-labels.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
export function SharedTrackPackagePanel({packet,onRead,disabled=false,supported=false,error,progress}){
 const locale=useManagementLocale(),text=trackPackageText(locale),number=new Intl.NumberFormat(locale),[preview,setPreview]=useState(null),[badImage,setBadImage]=useState(false),imageUrl=useRef();
 function close(){if(imageUrl.current)URL.revokeObjectURL(imageUrl.current);imageUrl.current=null;setPreview(null);}
 useEffect(()=>{close();setBadImage(false);return()=>{if(imageUrl.current)URL.revokeObjectURL(imageUrl.current);};},[packet]);
 function show(row){const file=packet.files.find(file=>file.id===row.id),info=file&&attachmentImageInfo(file.content);if(!info)return;close();setBadImage(false);const url=URL.createObjectURL(new Blob([file.content],{type:info.mime}));imageUrl.current=url;setPreview({row,url,info});}
 function download(row){const file=packet.files.find(file=>file.id===row.id);if(!file)return;const extension=({'image/png':'png','image/jpeg':'jpg','application/pdf':'pdf','text/plain':'txt','application/json':'json'})[row.mime]??'bin',url=URL.createObjectURL(new Blob([file.content],{type:'application/octet-stream'})),anchor=document.createElement('a');anchor.href=url;anchor.download=hex(row.hash)+'.'+extension;anchor.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
 return <section className="shared-track-package"><h4>{text.title}</h4><p>{text.intro}</p>
  {!supported&&<p>{text.unsupported}</p>}
  <Button disabled={disabled||!supported} onClick={onRead}>{text.read}</Button>
  {progress&&<p role="status"><bdi>{progress.reference??(progress.phase==='kml'?'KML':text.title)} · {number.format(progress.offset)} / {number.format(progress.total)} B</bdi></p>}
  {error&&<p role="alert">{trackStorageErrorText(locale,error)??(error==='TRACK_PACKAGE_UNSUPPORTED'?text.unsupported:text.error)}</p>}
  {packet&&!packet.attachmentIndex&&<p role="status">{text.indexMissing}</p>}
  {packet?.rows.map(row=>{const file=packet.files.find(file=>file.id===row.id),image=file&&attachmentImageInfo(file.content);return <article className="shared-track-resource" key={row.id}>
   <strong><bdi>{row.reference}</bdi></strong><p>{row.verified?text.verified:row.required?text.requiredMissing:text.optionalMissing} · <bdi>{number.format(row.bytes)} B</bdi></p>
   <p>{text[row.role===0?'photo':row.role===1?'icon':row.role===2?'overlay':'other']}</p>
   <div className="tracks-actions">{image&&<Button disabled={disabled} onClick={()=>show(row)}>{text.preview}</Button>}<Button disabled={disabled||!row.verified} onClick={()=>download(row)}>{text.download}</Button></div>
   <details><summary>{text.id} / SHA-256</summary><code dir="ltr">{row.id}</code><code dir="ltr">{hex(row.hash)}</code></details>
  </article>;})}
  {preview&&<figure className="shared-resource-preview"><figcaption><bdi>{preview.row.reference}</bdi> · <bdi>{number.format(preview.info.width)} × {number.format(preview.info.height)}</bdi></figcaption><img src={preview.url} alt={preview.row.reference} onError={()=>{setBadImage(true);close();}}/><Button onClick={close}>{text.close}</Button></figure>}
  {badImage&&<p role="alert">{text.error}</p>}
  <PackageArchiveExport packet={packet} disabled={disabled}/>
 </section>;
}
