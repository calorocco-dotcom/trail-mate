import React from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackConflictText} from '../../i18n/track-conflict-labels.js';
import {trackSharedText} from '../../i18n/track-shared-labels.js';
import {trackId,trackShareLink} from './shared-id.js';
import {trackVersionHistoryText} from '../../i18n/track-version-history-labels.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');

export function TrackConflictPanel({state,selected=false,selectedChainVerified=false,disabled=false,onOpen,onCopy}){
 const locale=useManagementLocale(),text=trackConflictText(locale),shared=trackSharedText(locale);
 const number=new Intl.NumberFormat(locale),date=new Intl.DateTimeFormat(locale,{dateStyle:'medium',timeStyle:'short',timeZone:'UTC'});
 const proof=state?.proof;
 if(!state)return null;
 return <section className="track-conflict-panel" aria-label={text.title}>
  <h4>{text.title}</h4>
  {!proof?<p role="alert">{state.invalid?text.invalid:text.unavailable}</p>:<>
   <p>{text.help}</p><p role="status">{text.verified}</p>
   <dl><dt>{shared.id}</dt><dd><code dir="ltr">{trackId(proof.objectId)}</code></dd><dt>{text.fingerprint}</dt><dd><code dir="ltr">{hex(proof.authorFingerprint)}</code></dd><dt>{text.revision}</dt><dd><bdi>{number.format(proof.revision)}</bdi></dd></dl>
   <div className="tracks-actions"><Button disabled={disabled} onClick={()=>onCopy?.(trackId(proof.objectId))}>{shared.copyId}</Button><Button disabled={disabled} onClick={()=>onCopy?.(trackShareLink(new URL('../',location.href).href,proof.objectId))}>{shared.copyLink}</Button></div>
   <div className="track-conflict-versions">{proof.versions.map((version,index)=><article key={hex(version.revisionHash)}>
    <h5>{text.content} <bdi>{number.format(index+1)}</bdi> · <bdi>{version.record[9]}</bdi></h5>
    <p>{version.record[6]===1?shared.archived:shared.active}</p><p><bdi>{version.record[10]}</bdi></p>
    <dl><dt>{shared.revision}</dt><dd><code dir="ltr">{hex(version.revisionHash)}</code></dd><dt>{text.updated}</dt><dd><bdi>{date.format(new Date(version.record[8]*1000))}</bdi></dd></dl>
    <Button disabled={disabled} onClick={()=>onOpen?.(version)}>{text.open}</Button>
   </article>)}</div>
   <p className="tracks-help">{selectedChainVerified?trackVersionHistoryText(locale).branch:text.chain}</p>{selected&&<p role="status">{text.historical}</p>}
  </>}
 </section>;
}
