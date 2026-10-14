import {defaultDurationUnit} from './config-duration.js';
import {configDurationText} from '../../i18n/config-duration-labels.js';
// Units are matched to the pinned module header and official module references.
// These are protobuf configuration values, not proof of runtime module support.
const units={
 'mqtt.map_report_settings.publish_interval_secs':'seconds',
 'serial.timeout':'milliseconds',
 'external_notification.output_ms':'milliseconds',
 'external_notification.nag_timeout':'seconds',
 'store_forward.history_return_window':'minutes',
 'range_test.sender':'seconds',
 'telemetry.device_update_interval':'seconds',
 'telemetry.environment_update_interval':'seconds',
 'telemetry.air_quality_interval':'seconds',
 'telemetry.power_update_interval':'seconds',
 'telemetry.health_update_interval':'seconds',
 'neighbor_info.update_interval':'seconds',
 'detection_sensor.minimum_broadcast_secs':'seconds',
 'detection_sensor.state_broadcast_secs':'seconds',
 'paxcounter.paxcounter_update_interval':'seconds',
};
export function moduleDuration(path,field){
 const nativeUnit=units[path.join('.')];if(!nativeUnit||field.type!=='UINT32')return undefined;
 return {nativeUnit,units:nativeUnit==='milliseconds'?[{key:'milliseconds',factor:1},{key:'seconds',factor:1000},{key:'minutes',factor:60000},{key:'hours',factor:3600000}]:nativeUnit==='minutes'?[{key:'minutes',factor:1},{key:'hours',factor:60}]:[{key:'seconds',factor:1},{key:'minutes',factor:60},{key:'hours',factor:3600}]};
}
export function validateModuleDuration(field,value){
 if(!Number.isSafeInteger(value)||value<(field.min??0)||value>field.max)throw Object.assign(Error('CONFIG_RANGE'),{code:'CONFIG_RANGE',params:{min:field.min??0,max:field.max}});
}
export function formatModuleDuration(path,field,locale){const spec=moduleDuration(path,field);if(!spec)return undefined;const unit=defaultDurationUnit(field.value,spec);return new Intl.NumberFormat(locale).format(field.value/unit.factor)+' '+configDurationText(locale)[unit.key];}
export function moduleDurationHint(path){const key=path.join('.');return key==='external_notification.nag_timeout'?'repeatOff':key==='detection_sensor.state_broadcast_secs'?'eventsOnly':key==='range_test.sender'?'senderOff':'zeroRetained';}
