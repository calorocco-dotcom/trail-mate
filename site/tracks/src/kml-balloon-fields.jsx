import React,{useEffect,useMemo,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {kmlBalloonText} from '../../i18n/kml-balloon-labels.js';
import {kmlOverlayEditorText} from '../../i18n/kml-overlay-editor-labels.js';
import {kmlBalloonVariables,previewKmlBalloon} from './kml-balloon.js';
const css=value=>value.color+Math.round(value.opacity*255).toString(16).padStart(2,'0');
export function KmlBalloonFields({model,index,form,onChange}){
 const locale=useManagementLocale(),text=kmlBalloonText(locale),overlay=kmlOverlayEditorText(locale),number=new Intl.NumberFormat(locale,{maximumFractionDigits:1}),input=useRef();
 const variables=useMemo(()=>kmlBalloonVariables(model,index),[model,index]),[token,setToken]=useState('');
 useEffect(()=>{if(!variables.some(value=>value.key===token))setToken(variables[0]?.key??'');},[variables]);
 const preview=useMemo(()=>{try{return previewKmlBalloon(model,index,form);}catch(error){return{error:error.code};}},[model,index,form]);
 function insert(){if(!token)return;const value='$['+token+']',start=Math.min(input.current?.selectionStart??form.text.length,form.text.length),end=Math.min(input.current?.selectionEnd??form.text.length,form.text.length);onChange('text',form.text.slice(0,start)+value+form.text.slice(end));const position=start+value.length;requestAnimationFrame(()=>{input.current?.focus();input.current?.setSelectionRange(position,position);});}
 return <details className="tracks-style-block kml-balloon-fields"><summary>{text.title}</summary><p className="tracks-help">{text.help}</p>
  <div className="tracks-fields">{['bgColor','textColor'].map((key,index)=>{const valid=/^[a-f\d]{8}$/i.test(form[key]),color=valid?form[key].toLowerCase():key==='bgColor'?'ffffffff':'ff000000',label=index?text.foreground:text.background,alpha=parseInt(color.slice(0,2),16);return <React.Fragment key={key}><label>{label}<input type="color" aria-label={text.title+' · '+label} value={'#'+color.slice(6,8)+color.slice(4,6)+color.slice(2,4)} onChange={event=>{const rgb=event.target.value.slice(1);onChange(key,color.slice(0,2)+rgb.slice(4,6)+rgb.slice(2,4)+rgb.slice(0,2));}}/></label><label>{label} · {overlay.opacity} · <bdi>{number.format(alpha/255*100)}%</bdi><input type="range" aria-label={text.title+' · '+label+' · '+overlay.opacity} aria-valuetext={number.format(alpha/255*100)+'%'} min="0" max="255" value={alpha} onChange={event=>onChange(key,Number(event.target.value).toString(16).padStart(2,'0')+color.slice(2))}/>{!valid&&<span role="status">{text.invalid} · <bdi>{form[key]}</bdi></span>}</label></React.Fragment>;})}</div>
  <label>{text.mode}<select aria-label={text.mode} value={form.displayMode} onChange={event=>onChange('displayMode',event.target.value)}><option value="default">{text.show}</option><option value="hide">{text.hide}</option>{!['default','hide'].includes(form.displayMode)&&<option value={form.displayMode} disabled>{form.displayMode}</option>}</select></label>
  <label>{text.template}<textarea ref={input} aria-label={text.template} rows={5} value={form.text} onChange={event=>onChange('text',event.target.value)}/></label>
  <div className="tracks-actions">{variables.length?<><label>{text.variable}<select aria-label={text.variable} value={token} onChange={event=>setToken(event.target.value)}>{variables.map(value=><option key={value.key} value={value.key}>{'$['+value.key+']'}</option>)}</select></label><Button disabled={!token} onClick={insert}>{text.insert}</Button></>:<p>{text.empty}</p>}<Button onClick={()=>onChange('text','$[name]\n$[description]')}>{text.basic}</Button></div>
  <p className="tracks-help">{text.context}</p><h3>{text.preview}</h3><p className="tracks-help">{text.plain}</p>
  {preview.error?<p role="alert">{text.limited}</p>:<>{preview.hidden?<p className="kml-balloon-preview">{text.hide}</p>:<pre className="kml-balloon-preview" style={{backgroundColor:css(preview.background),color:css(preview.foreground)}}>{preview.text}</pre>}{preview.unknown.length>0&&<p role="status">{text.unknown.replace('{tokens}',preview.unknown.join(', '))}</p>}</>}
 </details>;
}
