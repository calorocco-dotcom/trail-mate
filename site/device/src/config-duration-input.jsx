import React,{useState,useRef,useEffect} from 'react';
import {durationDisplay,defaultDurationUnit,parseDurationInput} from './config-duration.js';
import {validateConfigurationField} from './config-schema.js';
import {configDurationText} from '../../i18n/config-duration-labels.js';
export function ConfigurationDurationInput({record,schema,error,onEdit,onInvalid,disabled,id,accessibility,locale,validate=validateConfigurationField}){
 const text=configDurationText(locale),spec=schema.duration;
 const [factor,setFactor]=useState(()=>spec.units.find(unit=>unit.factor===error?.params?.durationFactor)?.factor??defaultDurationUnit(record.value,spec).factor);
 const [raw,setRaw]=useState(()=>error?.raw??durationDisplay(record.value,factor).text),lastValue=useRef(record.value);
 useEffect(()=>{if(record.value!==lastValue.current){lastValue.current=record.value;setRaw(durationDisplay(record.value,factor).text);}},[record.value,factor]);
 const display=durationDisplay(record.value,factor),native=text[spec.nativeUnit];
 return <div className="device-duration"><div className="device-duration-controls"><input id={id} {...accessibility} type="text" inputMode="decimal" dir="ltr" value={raw} disabled={disabled} onChange={event=>{
  const input=event.target.value;setRaw(input);try{const value=parseDurationInput(input,factor,locale);validate(record,value);onEdit(value,input);lastValue.current=value;}catch(failure){if(failure.code==='CONFIG_RANGE'){failure.code='DURATION_RANGE';failure.params={...failure.params,unit:native};}failure.params={...failure.params,durationFactor:factor};onInvalid(failure,input);}
 }}/><select aria-label={schema.label+' · '+text.unit} value={factor} disabled={disabled||Boolean(error)} onChange={event=>{const next=Number(event.target.value);setFactor(next);setRaw(durationDisplay(record.value,next).text);}}>{spec.units.map(unit=><option key={unit.factor} value={unit.factor}>{text[unit.key]}</option>)}</select></div>
 <output>{text.current.replace('{value}',new Intl.NumberFormat(locale).format(record.value)).replace('{unit}',native)}</output>
 {display.approximate&&<small>{text.approximate}</small>}<small>{text.precision.replace('{unit}',native)}</small></div>;
}
