import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackArchiveText} from '../../i18n/track-archive-labels.js';
import {exportTrackPackageArchive} from './package-archive.js';
export function PackageArchiveExport({packet,disabled=false}){
 const text=trackArchiveText(useManagementLocale()),[working,setWorking]=useState(false),[error,setError]=useState(false),epoch=useRef(0),abort=useRef(),url=useRef(),timer=useRef();
 useEffect(()=>{epoch.current++;abort.current?.abort();setWorking(false);setError(false);return()=>{epoch.current++;abort.current?.abort();if(url.current)URL.revokeObjectURL(url.current);clearTimeout(timer.current);};},[packet]);
 async function save(){if(working||disabled||!packet?.ready)return;const token=epoch.current,controller=new AbortController();abort.current=controller;setWorking(true);setError(false);
  try{const value=await exportTrackPackageArchive({signed:[packet.detail.manifest,packet.detail.signature],kml:packet.detail.source,attachmentIndex:packet.attachmentIndex,attachmentFiles:packet.files},{signal:controller.signal});if(token!==epoch.current)return;
   if(url.current)URL.revokeObjectURL(url.current);clearTimeout(timer.current);url.current=URL.createObjectURL(new Blob([value.bytes],{type:'application/zip'}));const anchor=document.createElement('a');anchor.href=url.current;anchor.download=value.filename;anchor.click();timer.current=setTimeout(()=>{if(url.current)URL.revokeObjectURL(url.current);url.current=null;},1000);
  }catch(failure){if(token===epoch.current&&failure.name!=='AbortError')setError(true);}finally{if(token===epoch.current)setWorking(false);}
 }
 return <div className="track-package-export"><p className="tracks-help">{text.hint}</p><Button disabled={disabled||working||!packet?.ready} onClick={save}>{working?text.working:text.export}</Button>{error&&<p role="alert">{text.failed}</p>}</div>;
}
