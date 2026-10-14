import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackIPText} from '../../i18n/track-ip-labels.js';
import {TrackDirectoryClient} from './protocol-client.js';
import {equal} from '../../geocaching/src/protocol.js';
import {parseTrackNetworkConfig} from './network-config.js';
import {readTrackTransportKey} from './transport-identity.js';
import {trackTransportText} from '../../i18n/track-transport-labels.js';
import './directories.css';

export function TrackIPConnection({disabled=false,onSelect,onSources,createClient=notify=>new TrackDirectoryClient(notify)}){
 const locale=useManagementLocale(),text=trackIPText(locale),client=useRef(),epoch=useRef(0),callback=useRef(onSelect);callback.current=onSelect;
 const choice=useRef(),refreshVersion=useRef(0),configuredSeeds=useRef([]);
 const autoStarted=useRef(false),[configuration,setConfiguration]=useState(null);
 const sourcesCallback=useRef(onSources);sourcesCallback.current=onSources;
 const [endpoint,setEndpoint]=useState(''),[seed,setSeed]=useState(''),[state,setState]=useState('initial'),[rows,setRows]=useState([]),[selected,setSelected]=useState(''),[working,setWorking]=useState(false);
 const [storageError,setStorageError]=useState(null);
 useEffect(()=>{let active=true;fetch(new URL('../geocaching/network.json',location.href)).then(response=>response.ok?response.json():null).then(config=>{if(!active||!config)return;const parsed=parseTrackNetworkConfig(config);configuredSeeds.current=parsed.discoverySeeds;setEndpoint(value=>value||parsed.endpoint);setConfiguration(parsed);}).catch(()=>{if(active)setState('error');});return()=>{active=false;epoch.current++;client.current?.close().catch(()=>{});callback.current?.(null);};},[]);
 useEffect(()=>{if(!configuration||disabled||autoStarted.current)return;autoStarted.current=true;connect(configuration);},[configuration,disabled]);
 async function refresh(instance=client.current,token=epoch.current){
  if(!instance||instance.closed)return;const version=++refreshVersion.current,generation=instance.generation,directories=await instance.verifiedDirectories();
  if(token!==epoch.current||client.current!==instance||version!==refreshVersion.current||generation!==instance.generation)return;
  const next=directories.map(row=>({...row,fileLimit:8388608,chunkLimit:1024,queryLimit:16}));setRows(next);sourcesCallback.current?.(next);
  const previous=choice.current?.directory,retained=previous&&next.find(row=>row.key===previous.key&&equal(row.publicKey,previous.publicKey)&&equal(row.delivery,previous.delivery)&&equal(row.epoch,previous.epoch));
  if(retained)setSelected(retained.key);else{const directory=next[0];choice.current=directory?{network:instance,directory}:undefined;setSelected(directory?.key??'');callback.current?.(choice.current??null);}
  setState(next.length?'ready':'discovering');
 }
 async function connect(settings){
  if(working||disabled)return;const token=++epoch.current;refreshVersion.current++;choice.current=undefined;setWorking(true);setStorageError(null);setRows([]);sourcesCallback.current?.([]);setSelected('');callback.current?.(null);setState('connecting');
  try{
   if(seed.trim()&&!/^[0-9a-f]{32}$/i.test(seed.trim()))throw Error('Invalid directory discovery address');
   await client.current?.close();
   const instance=createClient(event=>{
    if(token!==epoch.current)return;
    if(event.type==='sources'||event.type==='directory')refresh(client.current,token).catch(()=>setState('error'));
    if(event.type==='status'){setState(event.state==='disconnected'?'offline':'discovering');if(event.state==='disconnected'){refreshVersion.current++;choice.current=undefined;setRows([]);sourcesCallback.current?.([]);setSelected('');callback.current?.(null);}}
   });client.current=instance;
   const address=settings?.endpoint??endpoint.trim(),hints=settings?.discoverySeeds??(seed.trim()?[seed.trim().toLowerCase()]:configuredSeeds.current.slice());
   const transportKey=await readTrackTransportKey(address);
   try{if(token!==epoch.current)return;await instance.connect(address,transportKey,hints);}finally{transportKey.fill(0);}
   await refresh(instance,token);
  }catch(error){if(token===epoch.current){await client.current?.close().catch(()=>{});client.current=undefined;setStorageError(trackTransportText(locale,error.code)?error.code:null);setState('error');}}
  finally{if(token===epoch.current)setWorking(false);}
 }
 async function disconnect(){epoch.current++;refreshVersion.current++;choice.current=undefined;setWorking(false);const previous=client.current;client.current=undefined;setRows([]);sourcesCallback.current?.([]);setSelected('');callback.current?.(null);setState('offline');await previous?.close().catch(()=>{});}
 function choose(key){const directory=rows.find(row=>row.key===key);setSelected(directory?key:'');choice.current=directory?{network:client.current,directory}:undefined;callback.current?.(choice.current??null);}
 return <section className="track-directories" aria-label={text.title}><h4>{text.title}</h4>
  <details className="track-network-advanced"><summary>{text.advanced}</summary>
  <label>{text.endpoint}<input value={endpoint} onChange={event=>setEndpoint(event.target.value)} disabled={working||disabled||Boolean(client.current&&!client.current.closed)} type="url" dir="ltr"/></label>
  <label>{text.seed}<input value={seed} onChange={event=>setSeed(event.target.value)} disabled={working||disabled||Boolean(client.current&&!client.current.closed)} dir="ltr" autoComplete="off" spellCheck={false}/></label>
  </details>
  <div className="tracks-actions"><Button disabled={disabled||working||!endpoint||Boolean(client.current&&!client.current.closed)} onClick={()=>connect()}>{text.connect}</Button>
   <Button disabled={disabled||!client.current} onClick={disconnect}>{text.disconnect}</Button><Button disabled={disabled||working||!client.current||client.current.closed} onClick={()=>refresh().catch(()=>setState('error'))}>{text.refresh}</Button></div>
  {rows.length>0&&<label>{text.directory}<select aria-label={text.directory} value={selected} onChange={event=>choose(event.target.value)} disabled={disabled||working}><option value="">—</option>{rows.map(row=><option key={row.key} value={row.key}>{row.name||row.key}</option>)}</select></label>}
  <p role="status">{text[state]}</p>
  {storageError&&<p role="alert">{trackTransportText(locale,storageError)}</p>}
 </section>;
}
