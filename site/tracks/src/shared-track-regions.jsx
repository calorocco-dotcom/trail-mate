import React,{useEffect,useMemo,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackRegionText} from '../../i18n/track-region-labels.js';

export function SharedTrackRegions({onRead,onScope,disabled=false,selected}){
 const locale=useManagementLocale(),text=useMemo(()=>trackRegionText(locale),[locale]);
 const [path,setPath]=useState([]),[page,setPage]=useState(null),[loading,setLoading]=useState(false),[notice,setNotice]=useState(null);
 const epoch=useRef(0),controller=useRef(),busy=useRef(false),read=useRef(onRead);read.current=onRead;
 useEffect(()=>()=>{epoch.current++;controller.current?.abort();},[]);
 const name=node=>node.names[locale]??node.names[locale.split('-')[0]]??node.name;
 async function load(node=null,append=false){
  if(disabled||busy.current)return;
  const token=++epoch.current,abort=new AbortController();controller.current=abort;busy.current=true;setLoading(true);setNotice('loading');
  try{
   const result=await read.current(append?{type:'children',parent:path.at(-1)??null,after:page.cursor}:node?{type:'select',id:node.id}:{type:'children'},{signal:abort.signal});
   if(token!==epoch.current||abort.signal.aborted||!result)return;
   if(append)setPage(previous=>({...result,items:[...previous.items,...result.items]}));
   else{setPage(result);const index=node?path.findIndex(item=>item.id===node.id):-1;setPath(node?index<0?[...path,result.selected]:[...path.slice(0,index),result.selected]:[]);onScope(result.selected??null);}
   setNotice(result.items.length?null:'empty');
  }catch(error){if(token===epoch.current)setNotice(error.name==='AbortError'?null:error.code==='TRACK_REGION_UNSUPPORTED'?'unsupported':'error');}
  finally{if(token===epoch.current){busy.current=false;setLoading(false);}}
 }
 return <section className="shared-track-regions" aria-label={text.title}>
  <h4>{text.title}</h4><p className="tracks-help">{text.help}</p>
  <div className="tracks-actions"><Button disabled={disabled||loading} onClick={()=>load()}>{page?text.root:text.load}</Button>{selected&&<Button disabled={disabled||loading} onClick={()=>onScope(null)}>{text.map}</Button>}{loading&&<Button onClick={()=>controller.current?.abort()}>{text.cancel}</Button>}</div>
  {path.length>0&&<nav aria-label={text.title} className="tracks-actions">{path.map(node=><Button key={node.id} disabled={disabled||loading} onClick={()=>load(node)}><bdi>{name(node)}</bdi></Button>)}</nav>}
  {page&&<label>{text.select}<select aria-label={text.select} value="" disabled={disabled||loading||!page.items.length} onChange={event=>load(page.items.find(node=>node.id===event.target.value))}><option value="">{text.select}</option>{page.items.map(node=><option key={node.id} value={node.id}>{name(node)}</option>)}</select></label>}
  {page?.cursor&&<Button disabled={disabled||loading} onClick={()=>load(null,true)}>{text.more}</Button>}
  {notice&&<p role="status">{text[notice]}</p>}
 </section>;
}
