import React,{useEffect,useState} from 'react';
import './management-language.css';
import {ManagementCopyProvider} from './management-copy.jsx';

// Resolve against the published dist directory, not the source directory.
const runtimeUrl=new URL('../../i18n/management-runtime.js',import.meta.url);
let runtime;
let initialSnapshot;
function languageRuntime(){return runtime??=import(runtimeUrl.href);}
export async function initializeManagementPage(){
 const module=await languageRuntime();
 await module.initializeManagementLanguage();
 initialSnapshot=module.managementLanguageSnapshot();
 return initialSnapshot;
}
export function getManagementLanguageRuntime(){return languageRuntime();}
export function useManagementLanguage(){
 const [language,setLanguage]=useState(initialSnapshot);
 useEffect(()=>{
  let active=true,dispose;
  languageRuntime().then(module=>module.watchManagementLanguage(value=>{if(active)setLanguage(value);})).then(unsubscribe=>{if(active)dispose=unsubscribe;else unsubscribe();});
  return()=>{active=false;dispose?.();};
 },[]);
 const href=path=>{const url=new URL(path,location.href);url.searchParams.set('lang',language.locale);return url.href;};
 return {...language,href};
}
export function ManagementLanguage({page}){
 const language=useManagementLanguage(),[error,setError]=useState(false);
 useEffect(()=>setError(false),[language.locale]);
 useEffect(()=>{if(page==='device'||page==='tracks')document.title=`${page==='device'?language.deviceTitle:language.tracksTitle} · Trail Mate`;},[page,language]);
 return <div className="management-language"><label>{language.languageLabel}
  <select value={language.locale} aria-invalid={error||undefined} onChange={event=>{
   const value=event.target.value;
   languageRuntime().then(module=>module.changeManagementLanguage(value)).catch(()=>setError(true));
  }}>{language.locales.map(locale=><option key={locale.id} value={locale.id}>{locale.name}</option>)}</select>
 </label></div>;
}
export function ManagementPageProvider({children}){
 const language=useManagementLanguage();
 return <ManagementCopyProvider locale={language.locale}>{children}</ManagementCopyProvider>;
}
