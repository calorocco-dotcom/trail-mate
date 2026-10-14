import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {setConfigurationValue,synchronizeMeshCoreChannelMirror} from '../../site/device/src/config-editor-values.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {saveConfigDocument} from '../../site/device/src/config-save.js';
import {createConfigurationReadback,checkConfigurationReadback} from '../../site/device/src/config-readback.js';
import {OPERATION} from '../../site/device/src/management-protocol.js';
import {configurationPresentation} from '../../site/device/src/config-field-presentation.js';
import {configMeshChannelText} from '../../site/i18n/config-mesh-channel-labels.js';
import {locales} from '../../site/i18n/registry.js';

const key0=new Uint8Array(16).fill(11),key1=new Uint8Array(16).fill(22),base64=bytes=>Buffer.from(bytes).toString('base64');
function fixture(length=16){return new TmsDocument(new TextEncoder().encode(`TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmc.active_slot=u8:0\nmc.base.secondary_psk=b64:${base64(new Uint8Array(length).fill(11))}\nmc.channel.0.key=b64:${base64(key0)}\nmc.channel.1.key=b64:${base64(key1)}\nfuture.channel=opaque:preserve\nEND\n`));}
test('active channel changes synchronize only the derived mirror, including full zero padding and empty compatibility length',()=>{
 for(const length of [0,16,32]){
  const document=fixture(length),expected=new Uint8Array(length);expected.set(key1.subarray(0,Math.min(16,length)));setConfigurationValue(document,'mc.active_slot',1);assert.deepEqual(document.get('mc.base.secondary_psk').value,expected);assert.equal(document.get('future.channel').raw,'preserve');assert.deepEqual(document.get('mc.channel.0.key').value,key0);
  const before=document.serialize();synchronizeMeshCoreChannelMirror(document);assert.deepEqual(document.serialize(),before);
  const replacement=new Uint8Array(16).fill(33);setConfigurationValue(document,'mc.channel.0.key',replacement);assert.deepEqual(document.get('mc.base.secondary_psk').value,expected);
  setConfigurationValue(document,'mc.channel.1.key',replacement);expected.fill(0);expected.set(replacement.subarray(0,Math.min(16,length)));assert.deepEqual(document.get('mc.base.secondary_psk').value,expected);
 }
});
test('read-only mirror and invalid slots cannot mutate a document; future or partial formats are preserved',()=>{
 const document=fixture(),before=document.serialize();for(const [key,value] of [['mc.base.secondary_psk',key1],['mc.active_slot',8],['mc.active_slot',255]])assert.throws(()=>setConfigurationValue(document,key,value));assert.deepEqual(document.serialize(),before);
 for(const text of ['mc.active_slot=u8:0','mc.active_slot=u8:0\nmc.base.secondary_psk=opaque:future','mc.active_slot=u8:0\nmc.base.secondary_psk=b64:']){const partial=new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\n'+text+'\nEND\n')),original=partial.serialize();synchronizeMeshCoreChannelMirror(partial);assert.deepEqual(partial.serialize(),original);}
});
test('backup restoration uses real channel keys and slot, ignoring a stale derived mirror without altering the source',()=>{
 const current=fixture(),backup=new TmsDocument(fixture().serialize({kind:'backup'}));backup.set('mc.active_slot',1);backup.set('mc.channel.1.key',new Uint8Array(16).fill(47));backup.set('mc.base.secondary_psk',new Uint8Array(16).fill(99));
 const result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);assert.deepEqual(result.document.get('mc.base.secondary_psk').value,new Uint8Array(16).fill(47));assert.ok(result.skipped.some(item=>item.key==='mc.base.secondary_psk'));assert.deepEqual(current.get('mc.base.secondary_psk').value,key0);assert.deepEqual(backup.get('mc.base.secondary_psk').value,new Uint8Array(16).fill(99));
});
test('save freezes the synchronized upload, checkpoint and receipt digest for reconnect readback',async()=>{
 const document=fixture(),id=new Uint8Array(16).fill(7),baseHash=new Uint8Array(32).fill(8),author={verified:true,fingerprint:'11'.repeat(32)};document.set('mc.active_slot',1);
 let hash,offset=0,uploaded=[],check,commits=0;
 const session={async request(op,body){if(op===OPERATION.TRANSFER_BEGIN)hash=body[3];if(op===OPERATION.TRANSFER_CHUNK){uploaded.push(body[2]);offset+=body[2].length;}if(op===OPERATION.CONFIG_COMMIT){commits++;return [id,hash,1];}return [id,320,offset,op===OPERATION.TRANSFER_FINISH?1:0];}};
 const receipt=await saveConfigDocument(session,document,baseHash,{id,beforeCommit:value=>{assert.deepEqual(value.sha256,hash);check=createConfigurationReadback(new TmsDocument(value.source),author);}});
 const actual=new TmsDocument(new Uint8Array(Buffer.concat(uploaded)));assert.deepEqual(actual.get('mc.base.secondary_psk').value,key1);assert.deepEqual(checkConfigurationReadback(check,actual,author,receipt.sha256),{status:'matched',keys:[]});assert.equal(commits,1);
 assert.deepEqual(document.get('mc.base.secondary_psk').value,key0);assert.deepEqual(receipt.source,actual.originalBytes);
});
test('all languages expose the same eight slots and explain the read-only derived value',()=>{
 for(const {id} of locales){const text=configMeshChannelText(id),presentation=configurationPresentation('mc.active_slot',id),slots=configurationField({key:'mc.active_slot',type:'u8'},presentation).options;assert.deepEqual(slots.map(item=>item.value),[0,1,2,3,4,5,6,7]);assert.deepEqual(slots,text.meshCoreSlots);const mirror=configurationField({key:'mc.base.secondary_psk',type:'b64'},configurationPresentation('mc.base.secondary_psk',id));assert.ok(mirror.readOnly);assert.equal(mirror.help,text.meshCoreMirrorHelp);assert.throws(()=>validateConfigurationField({key:'mc.base.secondary_psk',type:'b64'},key0),error=>error.code==='CONFIG_READ_ONLY');}
});
