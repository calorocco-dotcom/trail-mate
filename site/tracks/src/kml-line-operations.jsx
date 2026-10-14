import React,{useEffect,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useKmlWorkspaceText} from '../../i18n/management-copy.jsx';
import {kmlLineOperationText} from '../../i18n/kml-line-operation-labels.js';
export function KmlLineOperations({editor,disabled,onAction}){
 const {locale}=useKmlWorkspaceText(),text=kmlLineOperationText(locale),count=editor?.coordinates.trim().split(/\n+/).length??0,number=new Intl.NumberFormat(locale);
 const [start,setStart]=useState('1'),[end,setEnd]=useState(String(count));useEffect(()=>{setStart('1');setEnd(String(count));},[editor]);
 const timed=editor?.type==='gx:Track',safe=!timed||editor.sequenceSafe;
 if(editor?.type!=='LineString'&&!timed)return <p className="tracks-help">{text.help}</p>;
 const first=Number(start),last=Number(end),cropValid=safe&&Number.isInteger(first)&&Number.isInteger(last)&&first>=1&&last<=count&&first<last,splitValid=safe&&editor.splitSafe!==false&&Number.isInteger(first)&&first>1&&first<count;
 return <fieldset className="tracks-line-operations" disabled={disabled}><legend>{text.title}</legend><p className="tracks-help">{timed?text.trackHelp:text.help}</p><p>{text.original}: {number.format(count)}</p>
  <div className="tracks-fields"><label>{text.start}<input type="number" min="1" max={count} step="1" value={start} onChange={event=>setStart(event.target.value)}/></label><label>{text.end}<input type="number" min="1" max={count} step="1" value={end} onChange={event=>setEnd(event.target.value)}/></label></div>
  <p>{text.kept}: {cropValid?number.format(last-first+1):'—'} · {text.segments}: {splitValid?number.format(first)+' / '+number.format(count-first+1):'—'}</p>
  {(!cropValid&&!splitValid)&&<p className="tracks-help">{text.invalid}</p>}
  {editor.splitSafe===false&&<p className="tracks-help">{text.splitIds}</p>}
  {!safe&&<p className="tracks-help">{text.trackUnsupported}</p>}
  <div className="tracks-actions"><Button disabled={disabled||timed} onClick={()=>onAction({action:'reverse'})}>{text.reverse}</Button><Button disabled={disabled||!cropValid} onClick={()=>onAction({action:'crop',start:first-1,end:last-1})}>{text.crop}</Button><Button disabled={disabled||!splitValid} onClick={()=>onAction({action:'split',start:first-1})}>{text.split}</Button></div>
  {!timed&&editor.segmentOperations?.inGroup&&<><p className="tracks-help">{text.mergeHelp}</p><p>{text.mergePoints}: {editor.segmentOperations.mergeNext?number.format(editor.segmentOperations.mergedPoints):'—'}</p><div className="tracks-actions"><Button disabled={disabled||!editor.segmentOperations.earlier} onClick={()=>onAction({action:'earlier'})}>{text.earlier}</Button><Button disabled={disabled||!editor.segmentOperations.later} onClick={()=>onAction({action:'later'})}>{text.later}</Button><Button disabled={disabled||!editor.segmentOperations.mergeNext} onClick={()=>onAction({action:'merge'})}>{text.merge}</Button></div></>}
 </fieldset>;
}
