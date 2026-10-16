import React,{useEffect,useRef,useState} from 'react';
import {TrackMetadata} from './track-metadata.jsx';
import {TrackAttachments} from './track-attachments.jsx';
export function TrackDraftDetails({draft,model,disabled=false,onSaved,onDirty,onBusy}){
 const [metadataDirty,setMetadataDirty]=useState(false),[metadataBusy,setMetadataBusy]=useState(false),[assetsDirty,setAssetsDirty]=useState(false),[assetsBusy,setAssetsBusy]=useState(false);
 const callbacks=useRef();callbacks.current={onDirty,onBusy};
 const dirty=metadataDirty||assetsDirty,busy=metadataBusy||assetsBusy;
 useEffect(()=>{callbacks.current.onDirty?.(dirty);return()=>callbacks.current.onDirty?.(false);},[dirty]);
 useEffect(()=>{callbacks.current.onBusy?.(busy);return()=>callbacks.current.onBusy?.(false);},[busy]);
 return <><TrackMetadata draft={draft} disabled={disabled||assetsDirty||assetsBusy} onSaved={onSaved} onDirty={setMetadataDirty} onBusy={setMetadataBusy}/>
  <TrackAttachments draft={draft} model={model} disabled={disabled||metadataDirty||metadataBusy} onSaved={onSaved} onDirty={setAssetsDirty} onBusy={setAssetsBusy}/></>;
}
