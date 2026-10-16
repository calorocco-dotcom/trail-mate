import React from 'react';
import {Button} from 'animal-island-ui';
import {validateDraftFields} from './device-draft.js';
import {useGeocachingDraftText} from '../../i18n/management-copy.jsx';
const errorKey=error=>({GEO_DRAFT_COORDINATE_FORMAT:'coordinateFormat',GEO_DRAFT_OPTIONS:'invalidOptions',GEO_DRAFT_COORDINATE_PAIR:'coordinatePair',GEO_DRAFT_COORDINATE_RANGE:'coordinateRange',GEO_DRAFT_TEXT:'textInvalid',GEO_DRAFT_FIELDS:'invalidFields'}[error.code]??'invalidFields');
const utf8=new TextEncoder();
export function draftEditorForm(draft){
  return {name:draft.name,description:draft.description,hint:draft.hint,
    latitude:draft.latitudeE7===null?'':String(draft.latitudeE7/1e7),longitude:draft.longitudeE7===null?'':String(draft.longitudeE7/1e7),
    state:String(draft.state),difficultyX2:String(draft.difficultyX2),terrainX2:String(draft.terrainX2),containerSize:String(draft.containerSize)};
}
export function draftEditorValue(form){
  const coordinate=value=>{if(!value.trim())return null;const number=Number(value);if(!Number.isFinite(number))throw Object.assign(Error('Enter valid coordinates.'),{code:'GEO_DRAFT_COORDINATE_FORMAT'});return Math.round(number*1e7);};
  return validateDraftFields({name:form.name,description:form.description,hint:form.hint,latitudeE7:coordinate(form.latitude),longitudeE7:coordinate(form.longitude),
    state:Number(form.state),difficultyX2:Number(form.difficultyX2),terrainX2:Number(form.terrainX2),containerSize:Number(form.containerSize)});
}
export function DraftEditor({value,onChange,editable,busy,pending,canSave,canAbort=true,onSave,onAbort,generation}){
  const {locale,text}=useGeocachingDraftText(),number=new Intl.NumberFormat(locale);
  const format=(key,values)=>text[key].replace(/\{(\w+)\}/g,(token,name)=>String(values[name]??token));
  let checked,error;try{checked=draftEditorValue(value);}catch(e){error=errorKey(e);}
  const field=(key,label,multiline,limit)=> <label className="geo-editor-field" key={key}>{label}
    {multiline?<textarea aria-label={label} aria-invalid={utf8.encode(value[key]).length>limit} className="geo-draft-text" value={value[key]} rows={key==='description'?6:2} onChange={e=>onChange(key,e.target.value)}/>:<input aria-label={label} aria-invalid={utf8.encode(value[key]).length>limit} value={value[key]} onChange={e=>onChange(key,e.target.value)}/>}
    <small>{format('bytes',{used:number.format(utf8.encode(value[key]).length),limit:number.format(limit)})}</small></label>;
  const rating=Array.from({length:9},(_,i)=>[String(i+2),number.format((i+2)/2)]);
  const select=(key,label,options)=><label className="geo-editor-field" key={key}>{label}<select aria-label={label} value={value[key]} onChange={e=>onChange(key,e.target.value)}>{options.map(([id,name])=><option key={id} value={id}>{name}</option>)}</select></label>;
  return <form className="geo-draft-editor" onSubmit={event=>{event.preventDefault();if(checked&&canSave&&!busy)onSave(checked);}}>
    <p>{format('generation',{generation:generation==null?text.unknown:number.format(generation)})} · {editable?text.deviceAuthor:text.readOnlyAuthor}</p>
    {error&&<p role="alert">{text[error]}</p>}
    <fieldset disabled={!editable||busy||pending}>
      {field('name',text.name,false,96)}
      <div className="geo-editor-pair">{['latitude','longitude'].map(key=><label className="geo-editor-field" key={key}>{text[key]}<input dir="ltr" aria-label={text[key]} type="number" step="0.0000001" value={value[key]} onChange={e=>onChange(key,e.target.value)}/></label>)}</div>
      <small>{text.coordinatesHelp}</small>
      {field('description',text.description,true,2048)}{field('hint',text.hint,true,512)}
      {select('state',text.state,[['0',text.active],['1',text.disabled],['2',text.archived]])}
      <div className="geo-editor-pair">{select('difficultyX2',text.difficulty,rating)}{select('terrainX2',text.terrain,rating)}</div>
      {select('containerSize',text.size,[['0',text.unknown],['1',text.micro],['2',text.small],['3',text.regular],['4',text.large],['5',text.other]])}
    </fieldset>
    {pending&&<p role="status">{text.unconfirmed}</p>}
    <div className="geo-maintenance-actions"><Button htmlType="submit" type="primary" disabled={!canSave||busy||Boolean(error)}>{pending?text.retrySave:text.save}</Button>{pending&&<Button disabled={busy||!canAbort} onClick={onAbort}>{text.abort}</Button>}</div>
    <p>{text.localOnly}</p>
  </form>;
}
