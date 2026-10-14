import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackDeviceSyncText} from '../../i18n/track-device-sync-labels.js';
import {readTrackDeviceSyncState,syncTrackIPPublication} from './device-sync-workflow.js';
import {restoreTrackDeviceHistory} from './device-history-restore.js';
import {trackDeviceRestoreText} from '../../i18n/track-device-restore-labels.js';

export function TrackDeviceSync({record,session,operations=[],disabled=false,acquire,release}){
 const text=trackDeviceSyncText(useManagementLocale()),[state,setState]=useState(null),[working,setWorking]=useState(false),[error,setError]=useState(false);
 const restoreText=trackDeviceRestoreText(useManagementLocale()),restoreSupported=[32,33,35,37,98].every(op=>operations.includes(op));
 const epoch=useRef(0),controller=useRef(),supported=[32,33,35,37,97].every(op=>operations.includes(op));
 useEffect(()=>{const token=++epoch.current;controller.current?.abort();setState(null);setError(false);setWorking(false);
  readTrackDeviceSyncState(record).then(value=>{if(token===epoch.current)setState(value);}).catch(()=>{if(token===epoch.current)setError(true);});
  return()=>{epoch.current++;controller.current?.abort();};
 },[record,session]);
 async function synchronize(restore=false){
  if(working||disabled||!session||!(restore?restoreSupported:supported))return;const lease=acquire?acquire():{};if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;setWorking(true);setError(false);
  try{const author=await proveDeviceAuthor(session,{signal:abort.signal});
   if(token!==epoch.current||abort.signal.aborted)return;
   const next=await (restore?restoreTrackDeviceHistory:syncTrackIPPublication)(session,record,author,{operations,signal:abort.signal});
   if(token===epoch.current)setState(next);
  }catch{if(token===epoch.current){setError(true);try{const next=await readTrackDeviceSyncState(record);if(token===epoch.current)setState(next);}catch{}}}
  finally{if(token===epoch.current)setWorking(false);release?.(lease);}
 }
 return <section aria-label={text.title}><h4>{text.title}</h4><p>{text.intro}</p>
  <p role="status">{state?.stage===2?text.complete:state?.stage===1?text.durable:text.pending}</p>
  {!supported&&<p>{text.unsupported}</p>}
  {state?.stage!==2&&<Button disabled={disabled||working||!session||!supported} onClick={()=>synchronize()}>{state?text.resume:text.sync}</Button>}
  {restoreSupported&&<><p>{restoreText.intro}</p><Button disabled={disabled||working||!session} onClick={()=>synchronize(true)}>{restoreText.restore}</Button></>}
  {working&&<Button onClick={()=>controller.current?.abort()}>{text.pause}</Button>}
  {error&&<p role="alert">{text.error}</p>}
 </section>;
}
