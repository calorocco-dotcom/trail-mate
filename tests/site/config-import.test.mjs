import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
const document=(kind,body)=>new TmsDocument(new TextEncoder().encode(`TMSET7\nschema.version=u16:7\ndocument.kind=enum:${kind}\n${body}\nEND\n`));
function collectionDocument(kind,wifi,interfaces=[{id:'lora',type:0}],extra=[]){
 const lines=['device.node_name=str:Old','extension.device=opaque:keep',`wifi.profile_count=u8:${wifi.length}`,`rt.net.interface_count=u8:${interfaces.length}`,...extra];
 wifi.forEach(([ssid,password],index)=>lines.push(`wifi.profile.${index}.ssid=str:${ssid}`,`wifi.profile.${index}.password=str:${password}`));
 for(let index=0;index<6;index++){
  const row=interfaces[index]??{},values={id:`str:${row.id??''}`,type:`u8:${row.type??0}`,enabled:'bool:0',
   target_host:`str:${row.host??''}`,target_port:'u16:4242',group_id:'str:reticulum',discovery_port:'u16:29716',data_port:'u16:42671'};
  for(const [field,value] of Object.entries(values))lines.push(`rt.net.interface.${index}.${field}=${value}`);
 }
 return document(kind,lines.join('\n'));
}
test('backup imports supported fields and preserves device-only and opaque fields',()=>{
 const current=document('working','device.node_name=str:Old\ngps.enabled=bool:1\nnew.device=opaque:keep');
 const backup=document('backup','device.node_name=str:New\nother.board=str:extra');
 const result=prepareConfigImport(current,backup);assert.equal(result.errors.length,0);assert.equal(result.changed.length,1);assert.equal(result.skipped.length,1);assert.equal(result.document.get('gps.enabled').value,true);assert.equal(result.document.get('new.device').raw,'keep');assert.equal(result.document.kind,'working');assert.equal(current.get('device.node_name').value,'Old');
});
test('type mismatches and invalid values prevent an unreviewed restore',()=>{
 const current=document('working','rt.net.interface.0.target_port=u16:4242');
 assert.equal(prepareConfigImport(current,document('backup','rt.net.interface.0.target_port=u16:0')).errors.length,1);
 assert.equal(prepareConfigImport(current,document('backup','rt.net.interface.0.target_port=str:wrong')).errors.length,1);
 assert.throws(()=>prepareConfigImport(current,current));
});

test('Wi-Fi backup replaces count and secrets while extensions follow their SSID',()=>{
 const current=collectionDocument('working',[['Home','old-secret'],['Camp','camp-secret']],undefined,
  ['wifi.profile.1.vendor=opaque:device-value','wifi.profile.1.known=str:old']);
 const backup=collectionDocument('backup',[['Camp','restored'],['New','']],undefined,
  ['wifi.profile.0.vendor=opaque:backup-value','wifi.profile.0.known=str:new','wifi.profile.1.unknown=str:skip']);
 const result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);
 assert.equal(result.document.get('wifi.profile.0.ssid').value,'Camp');assert.equal(result.document.get('wifi.profile.0.password').value,'restored');
 assert.equal(result.document.get('wifi.profile.0.vendor').raw,'device-value');assert.equal(result.document.get('wifi.profile.0.known').value,'new');
 assert.throws(()=>result.document.get('wifi.profile.1.vendor'));assert.throws(()=>result.document.get('wifi.profile.1.unknown'));
 assert.equal(result.document.get('extension.device').raw,'keep');assert.equal(current.get('wifi.profile.0.password').value,'old-secret');
 assert.deepEqual(result.skipped.map(item=>item.key).sort(),['extension.device','wifi.profile.0.vendor','wifi.profile.1.unknown'].sort());
 assert.ok(result.changed.some(item=>item.key==='wifi.profile.0.password'&&item.secret));
});
test('shrinking to zero removes all Wi-Fi credentials and records every deleted field',()=>{
 const current=collectionDocument('working',[['Home','old-secret'],['Camp','camp-secret']]),backup=collectionDocument('backup',[]);
 const result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);assert.equal(result.document.get('wifi.profile_count').value,0);
 assert.throws(()=>result.document.get('wifi.profile.0.password'));assert.throws(()=>result.document.get('wifi.profile.1.ssid'));
 assert.equal(result.changed.filter(item=>item.removed).length,4);assert.ok(result.changed.some(item=>item.removed&&item.secret));
});
test('a larger backup adds interfaces and a complete IFAC triple without losing fixed slots',()=>{
 const current=collectionDocument('working',[]),backup=collectionDocument('backup',[],[{id:'tcp',type:2,host:'gate.example.org'},{id:'lora',type:0}],
  ['rt.net.interface.0.network_name=str:Private','rt.net.interface.0.passphrase=str:restored-secret','rt.net.interface.0.ifac_size=u16:64']);
 const result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);assert.equal(result.document.get('rt.net.interface_count').value,2);
 assert.equal(result.document.get('rt.net.interface.0.passphrase').value,'restored-secret');assert.equal(result.document.get('rt.net.interface.5.target_port').value,4242);
 assert.equal(result.document.get('rt.net.interface.1.id').value,'lora');
});
test('interface extensions follow stable IDs and removed IFAC credentials do not survive in inactive slots',()=>{
 const current=collectionDocument('working',[],[{id:'tcp',type:2,host:'gate.example.org'},{id:'lora',type:0}],
  ['rt.net.interface.0.network_name=str:Private','rt.net.interface.0.passphrase=str:old-secret','rt.net.interface.0.ifac_size=u16:64',
   'rt.net.interface.1.vendor=opaque:lora-extension']);
 const backup=collectionDocument('backup',[]),result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);
 assert.equal(result.document.get('rt.net.interface.0.vendor').raw,'lora-extension');assert.throws(()=>result.document.get('rt.net.interface.0.passphrase'));
 assert.throws(()=>result.document.get('rt.net.interface.1.passphrase'));assert.equal(result.document.get('rt.net.interface.1.id').value,'');
});
test('invalid or partial collections roll back the complete import including scalar edits',()=>{
 const current=collectionDocument('working',[['Home','secret']]);
 for(const backup of [collectionDocument('backup',[['','']]),collectionDocument('backup',[['Same',''],['Same','']]),
  document('backup','device.node_name=str:New\nwifi.profile.0.ssid=str:Partial')]){
  const result=prepareConfigImport(current,backup);assert.ok(result.errors.length);assert.deepEqual(result.changed,[]);
  assert.deepEqual(result.document.serialize(),current.serialize());
 }
});
test('unsupported collection capability is skipped rather than injected into another device',()=>{
 const current=document('working','device.node_name=str:Old'),backup=collectionDocument('backup',[['Home','secret']]);
 const result=prepareConfigImport(current,backup);assert.deepEqual(result.errors,[]);assert.throws(()=>result.document.get('wifi.profile_count'));
 assert.ok(result.skipped.some(item=>item.reason.includes('集合能力')));
});

test('inactive slot corruption and retained inactive IFAC cannot enter a restored document',()=>{
 const current=collectionDocument('working',[['Home','secret']]);
 const inactiveAccess=collectionDocument('backup',[['Other','new']],undefined,
  ['rt.net.interface.1.network_name=str:Private','rt.net.interface.1.passphrase=str:inactive-secret','rt.net.interface.1.ifac_size=u16:64']);
 const invalidType=collectionDocument('backup',[['Other','new']]);invalidType.set('rt.net.interface.5.type',255);
 for(const backup of [inactiveAccess,invalidType]){
  backup.set('device.node_name','New');const result=prepareConfigImport(current,backup);
  assert.ok(result.errors.length);assert.deepEqual(result.changed,[]);assert.deepEqual(result.document.serialize(),current.serialize());
 }
});
test('incompatible extension types abort a collection restore rather than overwrite a device field',()=>{
 const current=collectionDocument('working',[['Home','secret']],undefined,['wifi.profile.0.known=str:device']);
 const backup=collectionDocument('backup',[['Home','new']],undefined,['wifi.profile.0.known=b64:YQ==']);
 const result=prepareConfigImport(current,backup);assert.ok(result.errors.some(error=>error.key==='wifi.profile.0.known'));
 assert.deepEqual(result.changed,[]);assert.deepEqual(result.document.serialize(),current.serialize());
});
