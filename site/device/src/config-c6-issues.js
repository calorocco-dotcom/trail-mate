import {validateC6Configuration} from './config-c6-validation.js';
export function configC6Issues(document){
 try{validateC6Configuration(document);return [];}catch(error){
  return [{key:error.field??'c6_companion',code:error.code==='C6_CONFIG_SSID_REQUIRED'?'COLLECTION_SSID_REQUIRED':error.code==='C6_CONFIG_AP_PASSWORD'?error.code:'COLLECTION_INVALID',params:{},message:error.code??'C6_CONFIG_INVALID'}];
 }
}
