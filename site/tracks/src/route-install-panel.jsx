import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {routeInstallText} from '../../i18n/route-install-labels.js';
import {beginRouteInstallation,restoreRouteInstallation,confirmRouteInstallation,forgetRouteInstallation} from './route-install-workflow.js';
import {routeInstallUnresolved} from './route-install-journal.js';
export function RouteInstallPanel({session,operations=[],source,disabled=false,acquire,release}){
 const locale=useManagementLocale(),text=routeInstallText(locale),number=new Intl.NumberFormat(locale);
 const [record,setRecord]=useState(null),[busy,setBusy]=useState(false),[error,setError]=useState(null),[progress,setProgress]=useState(null),[empty,setEmpty]=useState(false);
 const controller=useRef(),lease=useRef(),mounted=useRef(false);
 useEffect(()=>{mounted.current=true;return()=>{mounted.current=false;controller.current?.abort();if(lease.current){release?.(lease.current);lease.current=null;}};},[]);
 const supported=[1,2,32,33,34,35,85,95].every(op=>operations.includes(op));
 async function run(action){
  if(busy||disabled||!session||!supported)return;
  const acquired=acquire?.();if(!acquired)return;
  lease.current=acquired;controller.current=new AbortController();setBusy(true);setError(null);setProgress(null);setEmpty(false);
  try{const result=await action(controller.current.signal);if(mounted.current){setRecord(result);setEmpty(!result);}}
  catch(failure){if(mounted.current)setError(failure.code??'ROUTE_OPERATION_FAILED');}
  finally{if(lease.current===acquired){release?.(acquired);lease.current=null;}if(mounted.current)setBusy(false);}
 }
 const checkpoint=next=>{if(mounted.current)setRecord(next);};
 const status=record?record.stage==='installed'?text.installed:record.stage==='missing'?text.missing:record.stage==='rejected'?text.rejected:text.pending:empty?text.noRecord:'';
 return <section className="route-install-panel"><h4>{text.title}</h4>
  {!supported&&<p>{text.unsupported}</p>}
  <div className="tracks-actions">
   <Button disabled={busy||disabled||!session||!supported||!source||routeInstallUnresolved(record)} onClick={()=>{if(confirm(text.confirm))run(signal=>beginRouteInstallation(session,source,{operations,signal,onCheckpoint:checkpoint,onProgress:value=>{if(mounted.current)setProgress(value);}}));}}>{text.install}</Button>
   <Button disabled={busy||disabled||!session||!supported} onClick={()=>run(signal=>restoreRouteInstallation(session,{signal}))}>{text.restore}</Button>
   {record&&<Button disabled={busy||disabled||!session||!supported} onClick={()=>run(signal=>confirmRouteInstallation(session,record,{signal,onCheckpoint:checkpoint}))}>{text.query}</Button>}
   {record&&<Button disabled={busy||disabled} onClick={()=>{if(confirm(text.forgetConfirm))run(signal=>forgetRouteInstallation(record,{signal}));}}>{text.forget}</Button>}
  </div>
  <p role="status">{busy?text.working:status}{progress&&<> · <bdi>{number.format(progress.offset)} / {number.format(progress.total)} B</bdi></>}</p>
  {record&&<details><summary>KML · SHA-256</summary><code dir="ltr">{Array.from(record.sha256,byte=>byte.toString(16).padStart(2,'0')).join('')}</code></details>}
  {error&&<p role="alert">{error==='ROUTE_INSTALL_UNCONFIRMED'?text.pending:text.failed} · <bdi>{String(error)}</bdi></p>}
 </section>;
}
