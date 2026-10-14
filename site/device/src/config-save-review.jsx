import React,{useEffect,useId,useRef} from 'react';
import {Button} from 'animal-island-ui';
import {ConfigReview} from './config-review.jsx';
import {useConfigBackupText} from '../../i18n/management-copy.jsx';
import {ManagementLanguage} from '../../i18n/management-language.jsx';
import {configReticulumInterfaceText} from '../../i18n/config-reticulum-interface-labels.js';
export function ConfigSaveReview({review,locale,actions,help,onConfirm,onClose}){
 const dialog=useRef(),heading=useRef(),title=useId(),{text}=useConfigBackupText();
 const accessText=configReticulumInterfaceText(locale),label=review.noWrite?accessText.tidy:actions.save;
 useEffect(()=>{const modal=dialog.current,origin=window.document.activeElement;modal.showModal();heading.current.focus();return()=>{modal.close();if(origin?.isConnected)origin.focus();};},[]);
 return <dialog className="device-backup-dialog" ref={dialog} aria-labelledby={title} onCancel={event=>{event.preventDefault();onClose();}}>
  <h2 id={title} ref={heading} tabIndex={-1}>{label}</h2><ManagementLanguage page="device"/><p>{review.noWrite?accessText.emptyAccessReview:help}</p>
  {review.omitted?.length>0&&!review.noWrite&&<p>{accessText.normalized}</p>}
  <ConfigReview document={review.document} baseline={review.baseline} locale={locale}/>
  <div className="device-savebar"><Button onClick={onClose}>{text.cancel}</Button><Button type="primary" onClick={onConfirm}>{label}</Button></div>
 </dialog>;
}
