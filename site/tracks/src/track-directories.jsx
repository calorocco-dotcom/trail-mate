import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {OPERATION} from '../../device/src/management-protocol.js';
import {readTrackDirectories} from './device-directories.js';
import {parseDirectoryDiscoveryAddress,requestTrackDirectoryDiscovery} from './directory-discovery.js';
import './directories.css';
import {useTrackDirectoryText} from '../../i18n/management-copy.jsx';
const errorKey=error=>({DIRECTORY_ADDRESS_INVALID:'addressInvalid',DIRECTORY_ADDRESS_ZERO:'addressZero',DIRECTORY_KEY_MISMATCH:'keyMismatch'}[error.code]??'error');
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
const key=row=>hex(row.delivery)+':'+hex(row.epoch);

export function TrackDirectories({session,operations=[],disabled=false,acquire,release,onSelect}){
  const {text}=useTrackDirectoryText();
  const enabled=operations.includes(OPERATION.TRACK_DIRECTORY_LIST);
  const discoverSupported=operations.includes(OPERATION.TRACK_DIRECTORY_DISCOVER);
  const [address,setAddress]=useState('');
  const [rows,setRows]=useState([]),[selected,setSelected]=useState(''),[loading,setLoading]=useState(false),[notice,setNotice]=useState({key:'initial'});
  const epoch=useRef(0),controller=useRef(),running=useRef(false),choice=useRef(''),select=useRef(onSelect);select.current=onSelect;
  useEffect(()=>{
    epoch.current++;controller.current?.abort();running.current=false;setRows([]);setSelected('');setAddress('');choice.current='';setLoading(false);
    setNotice({key:enabled?'initial':'unsupported'});select.current?.(null);
    return()=>{epoch.current++;controller.current?.abort();running.current=false;};
  },[session,enabled]);
  async function refresh(){
    if(!session||!enabled||disabled||running.current)return;
    const lease=acquire?acquire():{};if(!lease)return;
    const token=epoch.current;running.current=true;setLoading(true);select.current?.(null);setNotice({key:'searching'});
    const abort=new AbortController();controller.current=abort;
    try{
      const next=await readTrackDirectories(session,{signal:abort.signal});
      if(token!==epoch.current)return;
      if(abort.signal.aborted){setNotice({key:'cancelled'});return;}
      const previous=choice.current,chosen=next.find(row=>key(row)===previous&&row.capabilitiesVerified);
      setRows(next);setSelected(chosen?key(chosen):'');choice.current=chosen?key(chosen):'';select.current?.(chosen??null);
      setNotice({key:previous&&!chosen?'changed':next.length?(next.some(row=>row.capabilitiesVerified)?'chooseReady':'negotiating'):'empty'});
    }catch(error){if(token===epoch.current){setRows([]);setSelected('');choice.current='';select.current?.(null);setNotice({key:error.name==='AbortError'?'cancelled':errorKey(error),code:error.code});}}
    finally{if(token===epoch.current){running.current=false;setLoading(false);}release?.(lease);}
  }
  function choose(value){
    const row=rows.find(item=>key(item)===value&&item.capabilitiesVerified)??null;
    choice.current=row?key(row):'';setSelected(choice.current);select.current?.(row);
  }
  async function lookup(){
    if(!session||!discoverSupported||disabled||running.current)return;
    let destination;try{destination=parseDirectoryDiscoveryAddress(address);}catch(error){setNotice({key:errorKey(error),code:error.code});return;}
    const lease=acquire?acquire():{};if(!lease)return;
    const token=epoch.current,abort=new AbortController();controller.current=abort;running.current=true;setLoading(true);setRows([]);setSelected('');choice.current='';select.current?.(null);setNotice({key:'requesting'});
    try{
      await requestTrackDirectoryDiscovery(session,destination,{signal:abort.signal});
      if(token===epoch.current)setNotice({key:abort.signal.aborted?'lookupCancelled':'requested'});
    }catch(error){if(token===epoch.current)setNotice({key:error.name==='AbortError'?'lookupCancelled':errorKey(error),code:error.code});}
    finally{if(token===epoch.current){running.current=false;setLoading(false);}release?.(lease);}
  }
  const chosen=rows.find(row=>key(row)===selected);
  return <section className="track-directories" aria-label={text.title}>
    <h4>{text.title}</h4>
    <p className="tracks-help">{text.intro}</p>
    <div className="tracks-actions"><Button disabled={!enabled||disabled||loading} onClick={refresh}>{text.refresh}</Button>{loading&&<Button onClick={()=>controller.current?.abort()}>{text.cancelRefresh}</Button>}</div>
    <p role="status">{text[notice.key]}{notice.key==='error'&&notice.code!=null&&<> · <bdi>{String(notice.code)}</bdi></>}</p>
    {discoverSupported&&<details><summary>{text.byAddress}</summary><label>{text.address}<input dir="ltr" aria-label={text.address} value={address} disabled={disabled||loading} onChange={event=>setAddress(event.target.value)} placeholder={text.placeholder} autoComplete="off" spellCheck={false}/></label><p className="tracks-help">{text.addressHelp}</p><Button disabled={disabled||loading} onClick={lookup}>{text.lookup}</Button></details>}
    {rows.length>0&&<><label>{text.publishDirectory}<select aria-label={text.publishDirectory} value={selected} disabled={disabled||loading||!rows.some(row=>row.capabilitiesVerified)} onChange={event=>choose(event.target.value)}>
      <option value="">{text.readyOption}</option>{rows.map(row=><option key={key(row)} value={key(row)} disabled={!row.capabilitiesVerified}>{row.name} · {row.capabilitiesVerified?text.ready:text.negotiatingShort}</option>)}
    </select></label><ul>{rows.map(row=><li key={key(row)}><strong><bdi>{row.name}</bdi></strong><span>{row.capabilitiesVerified?text.ready:text.waiting}</span><code dir="ltr">{hex(row.delivery)}</code></li>)}</ul></>}
    {chosen&&<p className="tracks-help">{text.selected.split('{name}')[0]}<bdi>{chosen.name}</bdi>{text.selected.split('{name}')[1]}</p>}
  </section>;
}
