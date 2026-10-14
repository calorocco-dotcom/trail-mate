import test from 'node:test';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';import {configExternalFieldText,configExternalFieldKeys} from '../../site/i18n/config-external-field-labels.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';import {configurationField} from '../../site/device/src/config-schema.js';
test('APRS and cellular fields use complete locale catalogs',()=>{
 for(const {id} of locales){const text=configExternalFieldText(id);assert.equal(Object.keys(text).length,configExternalFieldKeys.length);for(const value of Object.values(text))assert.ok(value.trim());
 for(const key of ['aprs.igate_callsign','aprs.enabled','aprs.node_map','aprs.position_interval_s','cellular.enabled','cellular.apn_password','cellular.smtp_security','cellular.microphone_gain']){const schema=configurationField({key,type:'str'},configurationPresentation(key,id));if(!['zh-Hans','zh-Hant-TW','ja'].includes(id))assert.ok(!/[\u3400-\u9fff]/.test(schema.label),`${id}: ${key}`);}
 }
});
test('external field labels preserve secret detection, ports and callsign lengths',()=>{
 for(const {id} of locales)for(const key of ['cellular.apn_password','cellular.smtp_password','cellular.smtp_port','aprs.igate_callsign','aprs.path']){const record={key,type:'str'},localized=configurationField(record,configurationPresentation(key,id)),baseline=configurationField(record);assert.equal(localized.secret,baseline.secret);assert.deepEqual(localized.range,baseline.range);assert.equal(localized.maxBytes,baseline.maxBytes);}
});
