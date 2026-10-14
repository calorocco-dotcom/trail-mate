import test from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {changeConfigCollection,configurationChanges} from '../../site/device/src/config-collections.js';
import {normalizeDisabledInterfaceAccess,prepareConfigurationSave} from '../../site/device/src/config-save-preparation.js';
import {saveConfigDocument} from '../../site/device/src/config-save.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configReticulumInterfaceKeys,configReticulumInterfaceText,interfaceAccessLengthOptions} from '../../site/i18n/config-reticulum-interface-labels.js';
import {locales} from '../../site/i18n/registry.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';
import {createConfigurationReadback,checkConfigurationReadback} from '../../site/device/src/config-readback.js';
import {configurationReview} from '../../site/device/src/config-review.js';
const encoder=new TextEncoder(),prefix='rt.net.interface.0.';
function fixture(){const rows=[];for(let index=0;index<6;index++){const p=`rt.net.interface.${index}.`;for(const [field,type,value] of [['id','str',index?'':'tcp'],['type','u8',index?0:2],['enabled','bool',0],['target_host','str',index?'':'gateway.invalid'],['target_port','u16',4242],['group_id','str','reticulum'],['discovery_port','u16',29716],['data_port','u16',42671]])rows.push(`${p}${field}=${type}:${value}`);}return new TmsDocument(encoder.encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Test\nrt.net.interface_count=u8:1\n'+rows.join('\n')+'\nrt.net.interface.0.vendor=opaque:Do%20Not%20Change\nEND\n'));}
const access=()=>changeConfigCollection(fixture(),'interfaces','access',0);
const digest=async source=>new Uint8Array(await webcrypto.subtle.digest('SHA-256',source));
const author={verified:true,fingerprint:'ab'.repeat(32)};
test('empty access normalization is nonmutating, idempotent and preserves unknown records',()=>{
 const input=access(),original=input.serialize(),result=prepareConfigurationSave(input);
 assert.deepEqual(input.serialize(),original);assert.equal(input.get(prefix+'ifac_size').value,0);assert.equal(result.omitted.length,3);
 assert.deepEqual([...configurationChanges(result.document,fixture())],[]);assert.equal(result.document.get(prefix+'vendor').raw,'Do%20Not%20Change');
 assert.deepEqual(prepareConfigurationSave(result.document).source,result.source);assert.deepEqual(prepareConfigurationSave(result.document).omitted,[]);
 const crlf=new TmsDocument(encoder.encode(new TextDecoder().decode(original).replaceAll('\n','\r\n')));assert.ok(new TextDecoder().decode(normalizeDisabledInterfaceAccess(crlf).source).includes('vendor=opaque:Do%20Not%20Change\r\n'));
});
test('nonempty name or passphrase and all valid bit lengths remain present',()=>{
 for(const field of ['network_name','passphrase'])for(let bits=8;bits<=512;bits+=8){const input=access();input.set(prefix+field,' x ');input.set(prefix+'ifac_size',bits);const result=prepareConfigurationSave(input);assert.deepEqual(result.omitted,[]);assert.equal(result.document.get(prefix+field).value,' x ');assert.equal(result.document.get(prefix+'ifac_size').value,bits);}
});
test('partial, wrongly typed, inactive or invalid access is not silently removed',()=>{
 const original=new TextDecoder().decode(access().serialize());
 const invalid=[original.replace(prefix+'passphrase=str:\n',''),original.replace(prefix+'ifac_size=u16:0',prefix+'ifac_size=u8:0'),original.replace('interface_count=u8:1','interface_count=u8:0'),original.replace(prefix+'ifac_size=u16:0',prefix+'ifac_size=u16:7')];
 for(const text of invalid){const input=new TmsDocument(encoder.encode(text));assert.deepEqual(normalizeDisabledInterfaceAccess(input).omitted,[]);assert.throws(()=>prepareConfigurationSave(input));}
 for(const bits of [1,7,9,511,513]){const input=access();input.set(prefix+'network_name','Private');input.set(prefix+'ifac_size',bits);assert.throws(()=>prepareConfigurationSave(input));}
});
test('all language catalogs explain lengths, ports and identity scope without losing custom values',()=>{
 const record={key:prefix+'ifac_size',type:'u16',value:24};
 for(const {id} of locales){const text=configReticulumInterfaceText(id);assert.deepEqual(Object.keys(text),configReticulumInterfaceKeys);for(const value of Object.values(text))assert.ok(value.length>0);
  const schema=configurationField(record,configurationPresentation(record.key,id));assert.equal(schema.help,text.bitsHelp);assert.equal(schema.allowCustom,true);assert.equal(schema.customLabel,text.custom);assert.equal(schema.step,8);assert.deepEqual(schema.options,interfaceAccessLengthOptions(id));assert.deepEqual(schema.options.map(row=>row.value),[0,8,16,32,64,128,256,512]);
  for(const [field,key] of [['target_port','tcpPortHelp'],['group_id','groupHelp'],['discovery_port','discoveryHelp'],['data_port','dataHelp'],['network_name','accessScope'],['passphrase','accessScope']])assert.equal(configurationField({key:prefix+field,type:'str'},configurationPresentation(prefix+field,id)).help,text[key]);
 }
 for(const value of [0,8,24,504,512])assert.doesNotThrow(()=>validateConfigurationField(record,value));for(const value of [1,7,9,511])assert.throws(()=>validateConfigurationField(record,value),{code:'CONFIG_IFAC_BITS'});
 for(const value of [-1,513])assert.throws(()=>validateConfigurationField(record,value),{code:'CONFIG_RANGE'});assert.throws(()=>validateConfigurationField(record,8.5),{code:'CONFIG_INTEGER'});
});
test('custom IFAC review includes both bit and byte counts and masks passphrases in every locale',()=>{
 const source=fixture(),changed=access();changed.set(prefix+'network_name','Private');changed.set(prefix+'passphrase','never-show-this-secret');changed.set(prefix+'ifac_size',24);
 for(const {id} of locales){const rows=configurationReview(changed,source,id),numbers=new Intl.NumberFormat(id),text=configReticulumInterfaceText(id);assert.equal(rows.find(row=>row.key===prefix+'ifac_size').after,text.bitsBytes.replace('{bits}',numbers.format(24)).replace('{bytes}',numbers.format(3)));assert.ok(!JSON.stringify(rows).includes('never-show-this-secret'));}
});
test('save transfer, approved bytes, checkpoint and returned bytes match the normalized document',async()=>{
 const input=access();input.set('device.node_name','Changed');const expected=prepareConfigurationSave(input).source,id=new Uint8Array(16).fill(1),base=new Uint8Array(32).fill(2),uploaded=[];let hash,checkpoint;
 const session={async request(op,body){if(op===OPERATION.TRANSFER_BEGIN)hash=body[3];if(op===OPERATION.TRANSFER_CHUNK)uploaded.push(...body[2]);if(op===OPERATION.CONFIG_COMMIT){assert.ok(checkpoint);assert.deepEqual(checkpoint.expected.serialize(),expected);return [id,hash,1];}return [id,320,uploaded.length,op===OPERATION.TRANSFER_FINISH?1:0];}};
 const result=await saveConfigDocument(session,input,base,{id,digest,beforeCommit:receipt=>{assert.deepEqual(receipt.source,expected);assert.deepEqual(receipt.sha256,hash);checkpoint=createConfigurationReadback(new TmsDocument(receipt.source),author);}});
 assert.deepEqual(Uint8Array.from(uploaded),expected);assert.deepEqual(result.source,expected);assert.deepEqual(result.sha256,await digest(expected));assert.equal(input.get(prefix+'ifac_size').value,0);
 assert.equal(checkConfigurationReadback(checkpoint,new TmsDocument(expected),author).status,'matched');
});
