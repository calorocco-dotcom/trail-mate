import React,{useEffect,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLanguage} from '../../i18n/management-language.jsx';
import {draftBackupText} from '../../i18n/draft-backup-labels.js';
import {readTrackDraft,saveTrackDraft} from './draft-store.js';
import {exportDraftBackup,readDraftBackup,DRAFT_BACKUP_LIMIT} from './draft-backup.js';
import {parseKml} from './kml.js';
export function DraftBackupControls({draft,disabled,onBusy,onRestored}){
 const {locale}=useManagementLanguage(),text=draftBackupText(locale),picker=useRef(),controller=useRef();
 const [phase,setPhase]=useState(null),[error,setError]=useState(false);
 useEffect(()=>()=>controller.current?.abort(),[]);
 async function run(kind,work){if(disabled||phase)return;const active=new AbortController();controller.current=active;setPhase(kind);setError(false);onBusy(true);
  try{await work(active.signal);}catch{if(!active.signal.aborted)setError(true);}finally{if(controller.current===active){controller.current=null;setPhase(null);onBusy(false);}}
 }
 function backup(){if(!draft)return;run('export',async signal=>{const snapshot=await readTrackDraft(draft.id);if(snapshot.version!==draft.version)throw Error('Draft changed');const value=await exportDraftBackup(snapshot,{signal});const url=URL.createObjectURL(new Blob([value.bytes],{type:'application/zip'})),anchor=document.createElement('a');anchor.href=url;anchor.download=value.filename;anchor.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});}
 function restore(file){if(!file)return;run('read',async signal=>{if(!file.size||file.size>DRAFT_BACKUP_LIMIT)throw Error('Backup size');const bytes=new Uint8Array(await file.arrayBuffer());if(signal.aborted)return;const snapshot=await readDraftBackup(bytes,{signal});parseKml(snapshot.source);if(signal.aborted)return;setPhase('save');const saved=await saveTrackDraft(snapshot.source,{name:snapshot.name,filename:snapshot.filename,draftRestore:snapshot});await onRestored(saved);});}
 return <div className="track-draft-backup"><div className="tracks-actions"><Button disabled={disabled||Boolean(phase)||!draft} onClick={backup}>{text.export}</Button><Button disabled={disabled||Boolean(phase)} onClick={()=>{picker.current.value='';picker.current.click();}}>{text.import}</Button>{phase==='read'&&<Button onClick={()=>controller.current?.abort()}>{text.cancel}</Button>}</div>
  <input data-draft-backup-picker ref={picker} hidden type="file" accept=".zip,application/zip" onChange={event=>restore(event.target.files[0])}/><small>{text.help}</small>{phase&&<p role="status">{text.working}</p>}{error&&<p role="alert">{text.failed}</p>}
 </div>;
}
