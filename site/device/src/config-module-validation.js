import {readMeshtasticModule,MODULE_LIMIT} from './meshtastic-module-document.js';
import {readStructuredModule} from './meshtastic-module-model.js';
export function validateKnownModuleDocument(document){
 const format=document.entries().find(record=>record.key==='mt_module.format');
 // Future formats remain opaque; never interpret them as the current protobuf.
 if(!format||format.value!==MODULE_LIMIT.format)return;
 readStructuredModule(readMeshtasticModule(document));
}
