import React,{useEffect,useRef,useState} from 'react';
import {Button,Card} from 'animal-island-ui';
import {HostlinkConnection} from '../../device/src/hostlink.js';
import {ManagementSession,OPERATION} from '../../device/src/management-protocol.js';
import {readDeviceTrackCatalogPage} from './device-catalog.js';
import {buildDeviceTrackSnapshot} from './device-snapshot-build.js';
import {readDeviceTrackSnapshot} from './device-snapshot.js';
import {TrackTaskStatus} from './track-task-status.jsx';
import {TrackHistory} from './track-history.jsx';
import {TrackSharedRead} from './track-shared-read.jsx';
import {TrackIPConnection} from './track-ip-connection.jsx';
import {TrackIPPublication} from './track-ip-publication.jsx';
import {RouteInstallPanel} from './route-install-panel.jsx';
import {useDeviceRecordText} from '../../i18n/management-copy.jsx';
const errorKey=error=>({CATALOG_CHANGED:'catalogChanged',CATALOG_BUSY:'busy',SNAPSHOT_HASH_MISMATCH:'hashMismatch',CAPABILITIES_INVALID:'capabilities'}[error.code]??'error');

export function DeviceRecords({onImport,onOpenDraft,disabled=false,draft,draftSource,onSharedDetail}){
 const {locale,text}=useDeviceRecordText(),number=new Intl.NumberFormat(locale);
 const [operations,setOperations]=useState([]);
 const [ipSelection,setIPSelection]=useState(null);
 const [ipSources,setIPSources]=useState([]);
 const importer=useRef(onImport);importer.current=onImport;
 const media=useRef(),snapshotSupported=useRef(false);
 const releaseSupported=useRef(false);
 const [phase,setPhase]=useState('');
 const [pendingRelease,setPendingRelease]=useState(null);
 const [rawFile,setRawFile]=useState(null),[rawUrl,setRawUrl]=useState('');
 useEffect(()=>{if(!rawFile){setRawUrl('');return;}const url=URL.createObjectURL(rawFile);setRawUrl(url);return()=>URL.revokeObjectURL(url);},[rawFile]);
 const connection=useRef(),session=useRef(),unsubscribe=useRef(),epoch=useRef(0),running=useRef(false),abort=useRef(),cursor=useRef(null),records=useRef([]);
 const [connected,setConnected]=useState(false),[supported,setSupported]=useState(false),[busy,setBusy]=useState(false),[rows,setRows]=useState([]),[more,setMore]=useState(false),[notice,setNotice]=useState({key:'initial'});
 const current=token=>epoch.current===token;
 useEffect(()=>{if(!connected)setPendingRelease(null);},[connected]);
 const finish=token=>{if(current(token)){running.current=false;setBusy(false);}};
 useEffect(()=>()=>{epoch.current++;abort.current?.abort();unsubscribe.current?.();session.current?.close();connection.current?.close().catch(()=>{});},[]);
 async function load(token,refresh=false){
  if(refresh){cursor.current=null;records.current=[];setRows([]);setMore(false);}
  abort.current?.abort();abort.current=new AbortController();const signal=abort.current.signal;
  let pages=0,added=0;
  do{
   const page=await readDeviceTrackCatalogPage(session.current,cursor.current,{signal});
   if(!current(token))return;
   cursor.current=page.cursor;
   media.current=page.media;
   if(page.entry){const index=records.current.findIndex(row=>row.name===page.entry.name);if(index<0){records.current.push(page.entry);added++;}else records.current[index]=page.entry;}
   pages++;
   setRows([...records.current]);setMore(cursor.current!==null);
  }while(cursor.current!==null&&pages<20&&added<8);
  if(current(token)){setRows([...records.current]);setMore(cursor.current!==null);setNotice({key:cursor.current===null?'listDone':'listMore'});}
 }
 async function connect(){
  if(running.current)return;
  if(!navigator.serial){setNotice({key:'noSerial'});return;}
  running.current=true;setBusy(true);const token=++epoch.current;
  let accepted=false;
  try{
   const port=await navigator.serial.requestPort();if(!current(token))return;
   const client=new HostlinkConnection(port);connection.current=client;await client.open();
   const management=new ManagementSession(client);session.current=management;
   const capabilities=await management.request(OPERATION.CAPABILITIES,[]);
   if(!Array.isArray(capabilities)||capabilities.length!==6||capabilities[0]!==1||!Array.isArray(capabilities[4]))throw Object.assign(Error('设备管理能力响应不兼容'),{code:'CAPABILITIES_INVALID'});
   setOperations(capabilities[4].slice());
   if(!current(token))return;
   unsubscribe.current=client.subscribe(event=>{if(current(token)&&event.type==='state'&&event.state==='disconnected'){epoch.current++;abort.current?.abort();running.current=false;setBusy(false);setConnected(false);setSupported(false);setMore(false);records.current=[];cursor.current=null;setRows([]);setNotice({key:'disconnectedRetry'});}});
   setConnected(true);accepted=true;const enabled=capabilities[4].includes(OPERATION.TRACK_LIST);setSupported(enabled);
   snapshotSupported.current=[OPERATION.TRACK_SNAPSHOT,OPERATION.TRACK_SNAPSHOT_BUILD,OPERATION.TRACK_SNAPSHOT_CANCEL].every(operation=>capabilities[4].includes(operation));
   releaseSupported.current=capabilities[4].includes(OPERATION.TRACK_SNAPSHOT_RELEASE);
   records.current=[];cursor.current=null;setRows([]);setMore(false);
   if(enabled)await load(token,true);else setNotice({key:'unsupported'});
  }catch(error){if(current(token)){setNotice({key:error.name==='NotFoundError'?'noDevice':errorKey(error),code:error.code});if(!accepted||connection.current?.state!=='ready'){setConnected(false);session.current?.close();await connection.current?.close();}}}
  finally{finish(token);}
 }
 async function read(refresh){if(running.current)return;const token=epoch.current;running.current=true;setBusy(true);try{await load(token,refresh);}catch(error){if(current(token))setNotice({key:errorKey(error),code:error.code});}finally{finish(token);}}
 async function disconnect(){epoch.current++;abort.current?.abort();unsubscribe.current?.();session.current?.close();running.current=false;setBusy(false);setConnected(false);setSupported(false);setMore(false);records.current=[];cursor.current=null;setRows([]);setNotice({key:'disconnected'});await connection.current?.close();}
 async function importRecord(row){
  if(running.current||disabled||pendingRelease||!snapshotSupported.current||!onImport)return;
  if(!confirm(text.importConfirm))return;
  const token=epoch.current;running.current=true;setBusy(true);setPhase('build');abort.current?.abort();abort.current=new AbortController();const signal=abort.current.signal;
  let built;
  try{
   built=await buildDeviceTrackSnapshot(session.current,row,media.current,{signal,onProgress:progress=>{if(current(token))setNotice({key:'build',name:row.name,offset:progress.offset,total:progress.total});}});
   if(!current(token)||signal.aborted)return;setPhase('read');
   const snapshot=await readDeviceTrackSnapshot(session.current,built.snapshotId,{signal,onProgress:progress=>{if(current(token))setNotice({key:'read',name:row.name,offset:progress.offset,total:progress.total});}});
   if(!current(token)||signal.aborted)return;
   if(snapshot.source.length!==built.bytes||snapshot.format!==built.format)throw Error('设备快照与选择的记录不一致');
   setPhase('import');setNotice({key:'saving'});
   const original=new File([snapshot.source],row.name);setRawFile(original);
   const imported=await importer.current(original);
   if(current(token))setNotice({key:imported?'imported':'notImported'});
  }catch(error){if(current(token))setNotice({key:error.name==='AbortError'?'cancelled':errorKey(error),code:error.code});}
  finally{
   if(built&&releaseSupported.current&&current(token))try{const result=await session.current.request(OPERATION.TRACK_SNAPSHOT_RELEASE,[built.snapshotId]);if(!Array.isArray(result)||result.length)throw Error('无效回收响应');}catch{if(current(token)){setPendingRelease({id:built.snapshotId.slice(),media:media.current});setNotice(value=>({...value,releasePending:true}));}}
   if(current(token))setPhase('');finish(token);
  }
 }
 async function retryRelease(){
  if(running.current||!pendingRelease)return;
  if(pendingRelease.media!==media.current){setPendingRelease(null);setNotice({key:'mediaChanged'});return;}
  const token=epoch.current;running.current=true;setBusy(true);
  try{const result=await session.current.request(OPERATION.TRACK_SNAPSHOT_RELEASE,[pendingRelease.id.slice()]);if(!Array.isArray(result)||result.length)throw Error('设备回收响应无效');if(current(token)){setPendingRelease(null);setNotice({key:'released'});}}
  catch(error){if(current(token))setNotice({key:'releaseFailed',code:error.code});}
  finally{finish(token);}
 }
 const acquire=()=>{if(running.current||pendingRelease)return false;running.current=true;setBusy(true);return {epoch:epoch.current};};
 const release=lease=>{if(lease.epoch===epoch.current){running.current=false;setBusy(false);}};
 return <Card className="device-records"><h3>{text.title}</h3>
  {pendingRelease&&connected&&<Button disabled={busy} onClick={retryRelease}>{text.retryRelease}</Button>}
  <div className="tracks-actions">{!connected?<Button disabled={busy} onClick={connect}>{text.connect}</Button>:<><Button onClick={disconnect}>{text.disconnect}</Button><Button disabled={busy||!supported} onClick={()=>read(true)}>{text.refresh}</Button></>}</div>
  <TrackIPConnection disabled={busy||Boolean(pendingRelease)} onSelect={setIPSelection} onSources={setIPSources}/>
  <TrackSharedRead session={connected?session.current:null} operations={connected?operations:[]} directory={ipSelection?.directory} queryDirectories={ipSources} network={ipSelection?.network} editorDraft={draft} disabled={disabled||busy||Boolean(pendingRelease)} acquire={acquire} release={release} onImport={onImport} onOpenDraft={onOpenDraft} onDetail={onSharedDetail}/>
  {connected&&<TrackIPPublication session={session.current} operations={operations} draft={draft} directory={ipSelection?.directory} network={ipSelection?.network} disabled={disabled||busy||Boolean(pendingRelease)} acquire={acquire} release={release} onOpenDraft={onOpenDraft}/>}
  {connected&&draft&&<section className="own-route-install"><p><bdi>{draft.name}</bdi></p><RouteInstallPanel session={session.current} operations={operations} source={draftSource} disabled={disabled||busy||Boolean(pendingRelease)} acquire={acquire} release={release}/></section>}
  {connected&&<TrackTaskStatus session={session.current} operations={operations} draft={draft} disabled={disabled||busy||Boolean(pendingRelease)} acquire={acquire} release={release}/>}
  {connected&&<TrackHistory session={session.current} operations={operations} draft={draft} disabled={disabled||busy||Boolean(pendingRelease)} acquire={acquire} release={release}/>}
  <p role="status">{text[notice.key]}{notice.name&&<> · <bdi>{notice.name}</bdi></>}{notice.total!=null&&<> · <bdi>{number.format(notice.offset)} / {number.format(notice.total)} B</bdi></>}{notice.key==='error'&&notice.code!=null&&<> · <bdi>{String(notice.code)}</bdi></>}{notice.releasePending&&<> {text.releasePending}</>}</p>
  {rawFile&&rawUrl&&<div className="tracks-actions"><a className="top-link" href={rawUrl} download={rawFile.name}>{text.download} <bdi>{rawFile.name}</bdi></a><Button onClick={()=>setRawFile(null)}>{text.clearRaw}</Button><small>{text.rawHelp}</small></div>}
  {busy&&['build','read'].includes(phase)&&<Button onClick={()=>abort.current?.abort()}>{text.cancelImport}</Button>}
  <div className="device-records-list">{rows.map(row=><div className="device-record-row" key={row.name}><strong><bdi>{row.name}</bdi></strong><small><bdi>{['KML','GPX','CSV','BIN'][row.format]} · {number.format(row.bytes)} B</bdi>{row.recording?' · '+text.recording:''}</small>{snapshotSupported.current&&onImport&&<Button disabled={busy||disabled||Boolean(pendingRelease)||row.recording||row.bytes===0n||row.bytes>8388608n||new TextEncoder().encode(row.name).length>117} onClick={()=>importRecord(row)} aria-label={text.importLabel.replace('{name}',row.name)}>{text.import}</Button>}</div>)}</div>
  {connected&&supported&&more&&<Button disabled={busy} onClick={()=>read(false)}>{text.more}</Button>}
  <p className="tracks-help">{text.help}</p>
 </Card>;
}
