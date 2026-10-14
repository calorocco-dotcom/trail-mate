import {configSchemaText} from '../../i18n/config-schema-labels.js';
import {configNetworkFieldText} from '../../i18n/config-network-field-labels.js';
import {configBasicFieldText} from '../../i18n/config-basic-field-labels.js';
import {configPositionText} from '../../i18n/config-position-labels.js';
import {configPositionFieldText} from '../../i18n/config-position-field-labels.js';
import {configSystemFieldText} from '../../i18n/config-system-field-labels.js';
import {configReferenceFieldText} from '../../i18n/config-reference-field-labels.js';
import {configRadioFieldText} from '../../i18n/config-radio-field-labels.js';
import {configChannelFieldText} from '../../i18n/config-channel-field-labels.js';
import {configExternalFieldText} from '../../i18n/config-external-field-labels.js';
import {configBluetoothText} from '../../i18n/config-bluetooth-labels.js';
import {configC6Text} from '../../i18n/config-c6-labels.js';
import {configC6PasswordText} from '../../i18n/config-c6-password-labels.js';
import {configBinaryText} from '../../i18n/config-binary-labels.js';
import {configMeshChannelText} from '../../i18n/config-mesh-channel-labels.js';
import {configRadioPolicyText} from '../../i18n/config-radio-policy-labels.js';
import {configTimezoneText} from '../../i18n/config-timezone-labels.js';
import {configMeaningText} from '../../i18n/config-meaning-labels.js';
import {radioRegionOptions} from './radio-region-options.js';
import {meshCoreRegionOptions} from './meshcore-region-presets.js';
import {configMeshCoreRegionText} from '../../i18n/config-meshcore-region-labels.js';
import {configCharacterText} from '../../i18n/config-character-labels.js';
import {configAprsSymbolNames} from '../../i18n/config-aprs-symbol-labels.js';
import {configPrecisionText} from '../../i18n/config-precision-labels.js';

export function configurationPresentation(key,locale){
 const meaning=configMeaningText(locale),presentation={...configSchemaText(locale),...configPositionText(locale),...configBluetoothText(locale),...configBinaryText(locale),...configMeshChannelText(locale),...configRadioPolicyText(locale),meaning,precision:configPrecisionText(locale),locale,radioRegions:radioRegionOptions(locale,meaning.unset)};
 if(key==='mc.region_preset')Object.assign(presentation,{meshCoreRegions:meshCoreRegionOptions(locale),meshCoreRegionText:configMeshCoreRegionText(locale)});
 if(key==='gps.motion_sensor'||/^aprs\.symbol_(table|code)$/.test(key))presentation.characters=configCharacterText(locale);
 if(/^aprs\.symbol_(table|code)$/.test(key))presentation.characters.symbolNames=configAprsSymbolNames(locale);
 if(key.startsWith('c6_companion.')){const fields=configC6Text(locale);return {...presentation,names:{c6_companion:fields.c6_companion},fields,c6ApPasswordHelp:configC6PasswordText(locale)};}
 if(/^(aprs|cellular)\./.test(key)){
  const fields={...configBasicFieldText(locale),...configExternalFieldText(locale)},numbers=new Intl.NumberFormat(locale);
  return {...presentation,names:{aprs:fields.aprs,cellular:fields.cellular},fields,index:value=>numbers.format(value)};
 }
 if(/^(ui|screen|display|chat|debug|power|privacy)\./.test(key)){
  const fields={...configBasicFieldText(locale),...configSystemFieldText(locale)},numbers=new Intl.NumberFormat(locale);
  const names=Object.fromEntries(['ui','screen','display','audio','haptics','ime','timezone','chat','alerts','auto_reply','debug','power','gauge','privacy'].map(name=>[name,fields[name]]));
  return {...presentation,...configTimezoneText(locale),names,fields,index:value=>numbers.format(value)};
 }
 if(/^(gps|map|route)\./.test(key)){
  const fields={...configBasicFieldText(locale),...configPositionFieldText(locale)},numbers=new Intl.NumberFormat(locale);
  return {...presentation,names:{gps:fields.gps,map:fields.map,route:fields.route},fields,index:value=>numbers.format(value)};
 }
 if(!/^(device|wifi|rt|mt|mc|protocol|policy|channel|network|mt_ble|mc_ble|mt_module|schema|document|legacy)\./.test(key))return presentation;
 const basic={...configBasicFieldText(locale),...configReferenceFieldText(locale),...configRadioFieldText(locale),...configChannelFieldText(locale)},numbers=new Intl.NumberFormat(locale);
 const names={device:basic.device,profile:basic.profile,group:basic.group,policy:basic.policy,protocol:basic.protocol,channel:basic.channel,mqtt:basic.mqtt,base:basic.base,...Object.fromEntries(['network','mt_ble','mc_ble','mt_module','schema','document','legacy','chunk'].map(name=>[name,basic[name]]))};
 const fields=key.startsWith('rt.net.')?{...basic,...configNetworkFieldText(locale)}:basic;
 if(key.startsWith('rt.net.'))Object.assign(names,{net:fields.network,interface:fields.interface,propagation:fields.propagation});
 return {...presentation,names,fields,index:value=>numbers.format(value)};
}
