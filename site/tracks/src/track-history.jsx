import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {listTrackJobHistory,removeTrackJobHistory,importTrackJobHistory} from './draft-store.js';
import {exportTrackJobHistory} from './export-job-history.js';
import {TrackTaskStatus} from './track-task-status.jsx';
import {useTrackHistoryText} from '../../i18n/management-copy.jsx';
const errorKey=error=>({HISTORY_INVALID:'invalid',HISTORY_CONFLICT:'conflict',HISTORY_FULL:'full',HISTORY_CHANGED:'changed'}[error.code]??'error');
export function TrackHistory({session,operations,disabled,acquire,release,draft}){
 const {locale,text}=useTrackHistoryText(),number=new Intl.NumberFormat(locale);
 const format=(key,values)=>text[key].replace(/\{(\w+)\}/g,(token,name)=>String(values[name]??token));
 const [rows,setRows]=useState([]),[selected,setSelected]=useState(''),[notice,setNotice]=useState(null),[refresh,setRefresh]=useState(0);
 const [working,setWorking]=useState(false);
 const fileInput=useRef();
 useEffect(()=>{let current=true;listTrackJobHistory().then(value=>{if(current){setRows(value);setSelected(key=>value.some(row=>row.key===key)?key:'');}}).catch(error=>{if(current)setNotice({key:errorKey(error)});});return()=>{current=false;};},[session,draft?.id,draft?.version,refresh]);
 const record=rows.find(row=>row.key===selected);
 async function importRecord(event){
  const file=event.target.files?.[0];event.target.value='';
  if(!file||disabled||working)return;setWorking(true);
  try{if(file.size>512){setNotice({key:'oversize'});return;}
   const row=await importTrackJobHistory(new Uint8Array(await file.arrayBuffer()),{name:file.name});
   setSelected(row.key);setRefresh(value=>value+1);setNotice({key:'imported'});
  }catch(error){setNotice({key:errorKey(error)});}finally{setWorking(false);}
 }
 async function exportRecord(){
  if(disabled||working||!record)return;setWorking(true);
  try{const file=await exportTrackJobHistory(record.key),url=URL.createObjectURL(new Blob([file.bytes],{type:'application/octet-stream'}));
   const link=document.createElement('a');link.href=url;link.download=file.filename;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
   setNotice({key:'exported'});
  }catch(error){setNotice({key:errorKey(error)});}finally{setWorking(false);}
 }
 async function removeRecord(){
  if(disabled||working||!record||!window.confirm(text.confirm))return;
  setWorking(true);try{await removeTrackJobHistory(record.key,new Uint8Array(record.binding));setSelected('');setRefresh(value=>value+1);setNotice({key:'removed'});}
  catch(error){setNotice({key:errorKey(error)});}finally{setWorking(false);}
 }
 return <section className="track-preparation"><h3>{text.title}</h3><p>{text.intro}</p>
  <label>{text.select}<select aria-label={text.select} value={selected} disabled={disabled||working} onChange={event=>setSelected(event.target.value)}><option value="">{format('count',{count:number.format(rows.length)})}</option>
   {rows.map(row=><option key={row.key} value={row.key}>{row.name} · {row.draftVersion?format('draftVersion',{version:number.format(row.draftVersion)}):text.importedRow} · {row.key.slice(-8)}</option>)}</select></label>
  <div className="tracks-actions"><Button disabled={disabled||working} onClick={()=>setRefresh(value=>value+1)}>{text.refresh}</Button>
   <Button disabled={disabled||working} onClick={()=>fileInput.current.click()}>{text.import}</Button><input ref={fileInput} hidden type="file" accept=".tmjob" aria-label={text.file} onChange={importRecord}/>
   {record&&<><Button disabled={disabled||working} onClick={exportRecord}>{text.export}</Button><Button disabled={disabled||working} onClick={removeRecord}>{text.remove}</Button></>}</div>
  <small>{text.help}</small>
  {notice&&<p role="status">{text[notice.key]}</p>}{record&&<TrackTaskStatus session={session} operations={operations} historyRecord={record} disabled={disabled||working} acquire={acquire} release={release}/>}
 </section>;
}
