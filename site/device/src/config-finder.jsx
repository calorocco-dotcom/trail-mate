import React,{useState} from 'react';
import {Button,Card} from 'animal-island-ui';
import {findConfigurationFields} from './config-finder.js';
import {configFinderText} from '../../i18n/config-finder-labels.js';
export function ConfigFinder({rows,changes,locale,onSelect}){
 const [query,setQuery]=useState(''),[changedOnly,setChangedOnly]=useState(false),[limit,setLimit]=useState(20);
 const text=configFinderText(locale),numbers=new Intl.NumberFormat(locale),active=Boolean(query.trim()||changedOnly);
 const results=active?findConfigurationFields(rows,query,{changedOnly,changes}):[];
 return <Card className="device-finder">
  <label className="device-field">{text.search}<input type="search" value={query} maxLength={256} onChange={event=>{setQuery(event.target.value);setLimit(20);}}/></label>
  <div className="device-finder-actions"><label className="device-toggle"><input type="checkbox" checked={changedOnly} onChange={event=>{setChangedOnly(event.target.checked);setLimit(20);}}/>{text.changedOnly}</label>
   {active&&<Button onClick={()=>{setQuery('');setChangedOnly(false);setLimit(20);}}>{text.clear}</Button>}
  </div>
  {active&&<><p role="status">{results.length?text.results.replace('{count}',numbers.format(results.length)):text.empty}</p>
   <ul className="device-finder-results">{results.slice(0,limit).map(row=><li key={row.key}><button onClick={()=>onSelect(row)}><strong>{row.label}</strong><span>{row.groupLabel}{changes.has(row.key)&&` · ${text.edited}`}</span><code dir="ltr">{row.key}</code></button></li>)}</ul>
   {results.length>limit&&<Button onClick={()=>setLimit(value=>value+20)}>{text.more}</Button>}
  </>}
 </Card>;
}
