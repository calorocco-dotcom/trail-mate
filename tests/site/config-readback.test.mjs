import test from 'node:test';
import assert from 'node:assert/strict';
import {createConfigurationReadback,checkConfigurationReadback} from '../../site/device/src/config-readback.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationReview} from '../../site/device/src/config-review.js';
import {configReadbackText} from '../../site/i18n/config-readback-labels.js';
import {locales} from '../../site/i18n/registry.js';
const author={verified:true,fingerprint:'a'.repeat(64),generation:7};
const document=()=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:Camp\nmt.mqtt.password=str:original-secret\nvendor.future=opaque:keep\nEND\n'));

test('f32 readback compares stored bits while detecting the smallest actual frequency change',()=>{
 const make=value=>new TmsDocument(new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nmc.frequency_mhz=f32:'+value+'\nEND\n'));
 const check=createConfigurationReadback(make(495.2),author);assert.equal(checkConfigurationReadback(check,make('495.200012'),author).status,'matched');
 // Next representable float, not a tolerance that could hide real changes.
 assert.deepEqual(checkConfigurationReadback(check,make('495.200043'),author),{status:'mismatch',keys:['mc.frequency_mhz']});
});

test('readback freezes submitted intent and accepts the same proven author after generation changes',()=>{
 const source=document(),check=createConfigurationReadback(source,author);
 source.set('device.node_name','later unsaved edit');
 assert.equal(check.expected.get('device.node_name').value,'Camp');
 assert.deepEqual(checkConfigurationReadback(check,document(),{...author,generation:8}),{status:'matched',keys:[]});
 assert.equal(source.get('device.node_name').value,'later unsaved edit');
});
test('different, missing and unverified authors cannot confirm a matching snapshot',()=>{
 const check=createConfigurationReadback(document(),author);
 assert.equal(checkConfigurationReadback(check,document(),{...author,fingerprint:'b'.repeat(64)}).status,'wrongDevice');
 for(const proof of [undefined,{...author,verified:false},{...author,fingerprint:'invalid'}])assert.equal(checkConfigurationReadback(check,document(),proof).status,'unverified');
 assert.equal(checkConfigurationReadback(createConfigurationReadback(document(),undefined),document(),author).status,'unverified');
});
test('device normalization and unknown field differences are reported without changing submitted configuration',()=>{
 const check=createConfigurationReadback(document(),author),actual=document();
 actual.set('device.node_name','Device default');actual.set('mt.mqtt.password','replacement-secret');
 const result=checkConfigurationReadback(check,actual,author);
 assert.equal(result.status,'mismatch');assert.deepEqual(result.keys.sort(),['device.node_name','mt.mqtt.password']);
 for(const {id} of locales){
  const rows=configurationReview(actual,check.expected,id),serialized=JSON.stringify(rows);
  assert.ok(!serialized.includes('original-secret'));assert.ok(!serialized.includes('replacement-secret'));
  const labels=configReadbackText(id);
  for(const key of ['title','pending','matched','mismatch','wrongDevice','unverified','differences','before','after'])assert.ok(labels[key].trim());
 }
 assert.equal(check.expected.get('mt.mqtt.password').value,'original-secret');
 const differentUnknown=new TmsDocument(new TextEncoder().encode(new TextDecoder().decode(actual.serialize()).replace('opaque:keep','opaque:changed')));
 assert.ok(checkConfigurationReadback(check,differentUnknown,author).keys.includes('vendor.future'));
});
