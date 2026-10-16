import {configurationChanges} from './config-collections.js';
import {configurationField} from './config-schema.js';
import {configurationPresentation} from './config-field-presentation.js';
import {configReviewText} from '../../i18n/config-review-labels.js';
import {configurationModuleReview} from './config-module-review.js';
import {configurationMaskOptions} from './config-mask.js';
import {configurationCharacter} from './config-character.js';
import {formatConfigurationDuration} from './config-duration.js';
import {isWebConfigurationField} from './config-web-scope.js';
import {isChannelPositionPrecision,formatPositionPrecision} from './config-position-precision.js';
import {formatConfigurationRadioTuning,formatConfigurationFloat} from './config-radio-tuning.js';
import {configReticulumInterfaceText} from '../../i18n/config-reticulum-interface-labels.js';
import {formatConfigurationFrequency} from './config-frequency.js';
import {formatConfigurationRadioValue} from './config-radio-values.js';
import {formatConfigurationNumericPurpose} from './config-numeric-purpose.js';
export function configurationReview(document,baseline,locale){
 const text=configReviewText(locale),numbers=new Intl.NumberFormat(locale),before=new Map(baseline?.entries().map(row=>[row.key,row])??[]),after=new Map(document.entries().map(row=>[row.key,row]));
 const format=(record,source)=>{
  if(!record)return text.missing;
  const schema=configurationField(record,configurationPresentation(record.key,locale),source);
  if(schema.secret)return text.secret;
  if(record.type==='b64'||record.value instanceof Uint8Array)return text.binary;
  if(record.value===undefined)return text.unsupported;
  if(record.type==='bool')return record.value?text.enabled:text.disabled;
  if(schema.percentScale)return new Intl.NumberFormat(locale,{style:'percent',maximumFractionDigits:1}).format(record.value/schema.percentScale);
  if(isChannelPositionPrecision(record.key)&&record.type==='u32')return formatPositionPrecision(record.value,locale);
  if(schema.character)return (schema.options.find(option=>option.value===record.value)?.label??(configurationCharacter(record.value)||schema.characterText.unset))+' ('+numbers.format(record.value)+')';
  const mask=configurationMaskOptions(record.key);if(mask){const selected=mask.filter(option=>record.value&option.value).map(option=>option.label);return selected.length?new Intl.ListFormat(locale).format(selected)+' ('+numbers.format(record.value)+')':numbers.format(record.value);}
  if(schema.options){const choice=schema.options.find(row=>row.value===record.value);if(choice)return choice.label;}
  if(schema.interfaceAccessLength)return configReticulumInterfaceText(locale).bitsBytes.replace('{bits}',numbers.format(record.value)).replace('{bytes}',numbers.format(record.value/8));
  const frequency=formatConfigurationFrequency(record,locale);if(frequency!==undefined)return frequency;
  const radio=formatConfigurationRadioValue(record,locale);if(radio!==undefined)return radio;
  const numeric=formatConfigurationNumericPurpose(record,locale);if(numeric!==undefined)return numeric;
  const duration=formatConfigurationDuration(record,locale);if(duration!==undefined)return duration;
  const tuning=formatConfigurationRadioTuning(record,locale);if(tuning!==undefined)return tuning;
  if(record.type==='f32'&&typeof record.value==='number')return formatConfigurationFloat(record.value,locale);
  if(typeof record.value==='number')return numbers.format(record.value);
  return String(record.value??'')||text.empty;
 };
 const changes=[...configurationChanges(document,baseline)].filter(isWebConfigurationField);let moduleRows;
 if(changes.some(key=>key.startsWith('mt_module.'))){try{moduleRows=configurationModuleReview(document,baseline,locale);}catch{}}
 return [...changes.filter(key=>!moduleRows||!key.startsWith('mt_module.')).map(key=>({key,label:configurationField(after.get(key)??before.get(key),configurationPresentation(key,locale)).label,
  before:format(before.get(key),baseline),after:format(after.get(key),document)})),...(moduleRows??[])];
}
