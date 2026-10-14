import React,{useState,useRef,useEffect} from 'react';
import {frequencyInputDisplay,parseFrequencyInput,formatConfigurationFrequency} from './config-frequency.js';
import {validateConfigurationField} from './config-schema.js';
import {configRadioValueText} from '../../i18n/config-radio-values-labels.js';
export function ConfigurationFrequencyInput({record,schema,error,onEdit,onInvalid,disabled,id,accessibility,locale,preferredUnit,onUnitChange}){
 const text=configRadioValueText(locale),[unit,setUnit]=useState(()=>schema.frequency.units.includes(error?.params?.frequencyUnit)?error.params.frequencyUnit:schema.frequency.units.includes(preferredUnit)?preferredUnit:'MHz');
 const [raw,setRaw]=useState(()=>error?.raw??frequencyInputDisplay(record.value,unit)),lastValue=useRef(record.value);
 useEffect(()=>{if(record.value!==lastValue.current){lastValue.current=record.value;setRaw(frequencyInputDisplay(record.value,unit));}},[record.value,unit]);
 return <div className="device-duration"><div className="device-duration-controls"><input id={id} {...accessibility} type="text" inputMode="decimal" dir="ltr" value={raw} disabled={disabled} onChange={event=>{const input=event.target.value;setRaw(input);try{const value=parseFrequencyInput(input,unit,locale);validateConfigurationField(record,value);onEdit(value,input);lastValue.current=value;}catch(failure){failure.params={...failure.params,frequencyUnit:unit};onInvalid(failure,input);}}}/><select aria-label={schema.label+' · '+text.unit} value={unit} disabled={disabled||Boolean(error)} onChange={event=>{const next=event.target.value;setUnit(next);onUnitChange(next);setRaw(frequencyInputDisplay(record.value,next));}}>{schema.frequency.units.map(unit=><option key={unit} value={unit}>{unit}</option>)}</select></div><output>{text.current.replace('{value}',()=>formatConfigurationFrequency(record,locale).replace(/ MHz$/,''))}</output><small>{text.precision}</small></div>;
}
