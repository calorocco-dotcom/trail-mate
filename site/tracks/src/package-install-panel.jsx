import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {packageInstallText} from '../../i18n/package-install-labels.js';
import {packageInstallHex} from './package-install-store.js';
import {beginPackageInstallation,restorePackageInstallation,queryPackageInstallation,resumePackageInstallation,restartPackageInstallation,forgetPackageInstallation} from './package-install-workflow.js';
export function PackageInstallPanel({session,operations=[],packet,disabled=false,acquire,release}){
 const locale=useManagementLocale(),text=packageInstallText(locale),number=new Intl.NumberFormat(locale),[task,setTask]=useState(null),[busy,setBusy]=useState(false),[error,setError]=useState(null),[progress,setProgress]=useState(null),[fresh,setFresh]=useState(false),[empty,setEmpty]=useState(false);
 const epoch=useRef(0),controller=useRef(),mounted=useRef(true);
 useEffect(()=>{epoch.current++;controller.current?.abort();setTask(null);setBusy(false);setFresh(false);setEmpty(false);setError(null);},[session]);
 useEffect(()=>{mounted.current=true;return()=>{mounted.current=false;epoch.current++;controller.current?.abort();};},[]);
 const supported=[1,2,32,33,35,100,101].every(op=>operations.includes(op)),unresolved=task&&['uploading','unknown'].includes(task.stage);
 async function run(action,verify=false){if(busy||disabled||!session||!supported)return;const lease=acquire?acquire():{};if(!lease)return;
  const token=epoch.current,abort=new AbortController();controller.current=abort;setBusy(true);setError(null);setProgress(null);setFresh(false);setEmpty(false);
  const current=()=>mounted.current&&token===epoch.current;
  try{const next=await action(session,{operations,signal:abort.signal,onCheckpoint:value=>{if(current())setTask(value);},onProgress:value=>{if(current())setProgress(value);}});if(current()){setTask(next);setEmpty(!next);setFresh(verify&&Boolean(next));}}
  catch(failure){if(current())setError(failure.name==='AbortError'?null:failure.code??'PACKAGE_OPERATION_FAILED');}
  finally{release?.(lease);if(current())setBusy(false);}
 }
 const status=task?.stage==='installed'?text.installed:task?.stage==='missing'?text.missing:task?text.pending:empty?text.noRecord:'';
 return <section className="package-install-panel"><h4>{text.title}</h4><p>{text.intro}</p>
  {!supported&&<p>{text.unsupported}</p>}{!packet?.ready&&<p>{text.incomplete}</p>}
  <div className="tracks-actions"><Button disabled={disabled||busy||!session||!supported||!packet?.ready||Boolean(unresolved)} onClick={()=>{if(confirm(text.confirm))run((session,options)=>beginPackageInstallation(session,packet,options),true);}}>{text.install}</Button>
   <Button disabled={disabled||busy||!session||!supported} onClick={()=>run(restorePackageInstallation)}>{text.restore}</Button>
   {task&&<><Button disabled={disabled||busy||!session||!supported} onClick={()=>run(queryPackageInstallation,true)}>{text.query}</Button><Button disabled={disabled||busy||!session||!supported} onClick={()=>run(resumePackageInstallation,true)}>{text.resume}</Button><Button disabled={disabled||busy||!session||!supported} onClick={()=>{if(confirm(text.forgetConfirm))run(forgetPackageInstallation);}}>{text.forget}</Button></>}
   {task?.stage==='missing'&&<Button disabled={disabled||busy||!session||!supported} onClick={()=>{if(confirm(text.confirm))run(restartPackageInstallation,true);}}>{text.restart}</Button>}
   {busy&&<Button onClick={()=>controller.current?.abort()}>{text.cancel}</Button>}
  </div><p role="status">{busy?text.working:status}</p>{task&&!busy&&<p>{fresh?text.fresh:text.cached}</p>}
  {progress?.total>0&&<p><bdi>{number.format(progress.offset)} / {number.format(progress.total)} B</bdi></p>}
  {task&&<details><summary><bdi>{task.title??'KML'}</bdi> · {number.format(task.bytes)} B</summary><code dir="ltr">{packageInstallHex(task.objectId)}</code><code dir="ltr">{packageInstallHex(task.revisionHash)}</code></details>}
  {error&&<p role="alert">{error==='PACKAGE_GENERATION_CHANGED'?text.generation:text.failed}</p>}
 </section>;
}
