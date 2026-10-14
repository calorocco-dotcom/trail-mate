import {configPrecisionText} from '../../i18n/config-precision-labels.js';
// Official channel reference scales, not an inferred GPS error/privacy radius.
const distances=new Map([[10,23300],[11,11700],[12,5800],[13,2900],[14,1500],[15,729],[16,364],[17,182],[18,91],[19,45]]);
export function isChannelPositionPrecision(key){return /^channel\.(primary|secondary)_position_precision$/.test(key);}
export function isModulePositionPrecision(path,field){return path.join('.')==='mqtt.map_report_settings.position_precision'&&field.type==='UINT32';}
export function formatPositionPrecision(value,locale,{map=false}={}){
 const text=configPrecisionText(locale),number=new Intl.NumberFormat(locale);if(value===0)return map?text.zeroStored:text.offProtocol;if(value===32)return text.fullProtocol;
 if(!Number.isInteger(value)||value<0||value>32)return text.nonstandard.replace('{value}',number.format(value));
 const bits=text.bits.replace('{value}',number.format(value)),meters=distances.get(value);if(!meters)return bits;
 const distance=new Intl.NumberFormat(locale,{style:'unit',unit:meters>=1000?'kilometer':'meter',maximumFractionDigits:1}).format(meters>=1000?meters/1000:meters);return text.approx.replace('{distance}',distance).replace('{bits}',bits);
}
export function positionPrecisionOptions(locale,options){return [0,...distances.keys(),32].map(value=>({value,label:formatPositionPrecision(value,locale,options)}));}
