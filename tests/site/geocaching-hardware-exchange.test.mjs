// No physical port is opened. Exercise the acceptance bridge's write boundary.
import assert from 'node:assert/strict';
import {GeocachingHardwareExchange} from '../../tools/hostlink/geocaching_hardware_exchange.mjs';
import {encode} from '../../site/geocaching/src/protocol.js';
import {encodeFrame,FRAME} from '../../site/device/src/hostlink.js';
import {encodeManagement} from '../../site/device/src/management-protocol.js';
const id=new Uint8Array(16).fill(1),other=new Uint8Array(16).fill(2),hash=new Uint8Array(32),session=new Uint8Array(16).fill(3),request=new Uint8Array(16).fill(4);
const gate=Object.create(GeocachingHardwareExchange.prototype);
Object.assign(gate,{namePrefix:'acceptance-',transfers:new Map(),drafts:new Set(),signatures:new Set(),confirmations:new Set(),confirmed:new Set(),calls:[],broken:null});
assert.throws(()=>gate.guard({code:32,body:[id,2,100,hash]}),/Device author/);
gate.fingerprint='expected';gate.author={fingerprint:'expected'};
let rejected=0;
const reject=action=>{assert.throws(action);rejected++;};
for(const kind of [0,1,3,4,7])reject(()=>gate.guard({code:32,body:[id,kind,100,hash]}));
for(const size of [0,4167])reject(()=>gate.guard({code:32,body:[id,2,size,hash]}));
gate.guard({code:32,body:[id,2,100,hash]});
reject(()=>gate.guard({code:32,body:[id,5,100,hash]}));
for(const code of [33,34,35,36,37])reject(()=>gate.guard({code,body:[other,0,new Uint8Array(1)]}));
reject(()=>gate.guard({code:33,body:[id,99,new Uint8Array(2)]}));
const fields=[0,310000000,1210000000,'acceptance-test','body','hint',2,2,0];
const edit=(values=fields,key=null,base=null)=>encode([key,base,values,1]);
const put=(raw,expected=0,draft=id)=>{gate.transfers.set(Buffer.from(id).toString('hex'),{kind:2,sealed:true,content:raw});gate.guard({code:66,body:[id,draft,expected,2]});};
put(edit());
reject(()=>put(edit(),1,other));
reject(()=>put(edit([...fields.slice(0,3),'unrelated-existing',...fields.slice(4)])));
reject(()=>put(edit([0,300000000,...fields.slice(2)])));
reject(()=>put(edit(fields,new Uint8Array(64),hash)));
reject(()=>gate.guard({code:65,body:[other]}));
gate.drafts.add(Buffer.from(id).toString('hex'));put(edit(),2);gate.guard({code:65,body:[id]});
for(const code of [72,73,74,76,77])reject(()=>gate.guard({code,body:[other]}));
for(const code of [7,16,17,20,21,67,68,70,96,97,98,99,100,101]){
 await assert.rejects(gate.run(encodeFrame(FRAME.MANAGEMENT_REQUEST,1,encodeManagement(session,request,code,[]))),/unrelated operation/);rejected++;
}
assert.deepEqual(gate.calls,[]);
assert.throws(()=>new GeocachingHardwareExchange({port:'COM69',python:'unused',fingerprint:'wrong',namePrefix:'acceptance-'}));
console.log('Hardware Geocaching acceptance gate: '+rejected+' unrelated/invalid writes rejected without opening a port.');
