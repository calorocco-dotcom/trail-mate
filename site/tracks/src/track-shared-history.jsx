import React from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackVersionHistoryText} from '../../i18n/track-version-history-labels.js';
import {trackSharedText} from '../../i18n/track-shared-labels.js';
import {trackStorageErrorText} from '../../i18n/track-storage-labels.js';
import {equal} from '../../geocaching/src/protocol.js';
import {trackShareLink} from './shared-id.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
export function TrackSharedHistory({state,error,selectedHash,disabled=false,reading=false,onRead,onCancel,onOpen,onCopy}){
 const locale=useManagementLocale(),text=trackVersionHistoryText(locale),shared=trackSharedText(locale),number=new Intl.NumberFormat(locale),date=new Intl.DateTimeFormat(locale,{dateStyle:'medium',timeStyle:'short',timeZone:'UTC'});
 return <section className="track-shared-history" aria-label={text.title}>
  <h4>{text.title}</h4><p>{text.intro}</p>
  <div className="tracks-actions"><Button disabled={disabled} onClick={onRead}>{state?text.retry:text.read}</Button>{reading&&<Button onClick={onCancel}>{shared.cancel}</Button>}</div>
  {state&&!error&&<p role="status">{text[state.status].replace('{count}',number.format(state.versions.length))}</p>}
  {error&&<p role="alert">{trackStorageErrorText(locale,error)??(error==='TRACK_HISTORY_PROOF_INVALID'?text.invalid:text.failed)}</p>}
  <ol>{state?.versions.map(version=><li key={hex(version.revisionHash)}>
   <h5><bdi>{number.format(version.record[4])} · {version.record[9]}</bdi></h5><p>{version.record[6]===1?shared.archived:shared.active}</p>
   <dl><dt>{shared.revision}</dt><dd><code dir="ltr">{hex(version.revisionHash)}</code></dd><dt>{text.updated}</dt><dd><bdi>{date.format(new Date(version.record[8]*1000))}</bdi></dd></dl>
   <div className="tracks-actions">{equal(selectedHash,version.revisionHash)?<span>{text.selected}</span>:<Button disabled={disabled} onClick={()=>onOpen?.(version)}>{text.open}</Button>}<Button disabled={disabled} onClick={()=>onCopy?.(trackShareLink(new URL('../',location.href).href,version.objectId,{revision:version.revisionHash}))}>{shared.copyLink}</Button></div>
  </li>)}</ol>
 </section>;
}
