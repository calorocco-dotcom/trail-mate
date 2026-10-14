// Read-only catalogue diagnosis. Only matching isolated acceptance names are logged.
import {GeocachingHardwareExchange} from './geocaching_hardware_exchange.mjs';
import {encodeFrame,FrameDecoder,FRAME} from '../../site/device/src/hostlink.js';
import {encodeManagement,decodeManagement} from '../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../site/device/src/author-identity.js';
import {readDeviceDraftPage} from '../../site/geocaching/src/device-draft-catalog.js';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
if(!['--port','--python','--expected-author'].every(flag=>args.includes(flag)))throw Error('Explicit port/Python/expected author required');
const hardware=new GeocachingHardwareExchange({port:option('--port'),python:option('--python'),fingerprint:option('--expected-author'),namePrefix:'Wio网页验收-'});
let sequence=0;
async function exchange(type,payload){const raw=await hardware.exchange(encodeFrame(type,++sequence,payload));return new FrameDecoder().push(Uint8Array.from(raw))[0].payload;}
const session={id:crypto.getRandomValues(new Uint8Array(16)),async request(code,body){if(![0,1,2,3,64].includes(code))throw Error('Read-only catalogue probe rejected operation');const value=decodeManagement(await exchange(FRAME.MANAGEMENT_REQUEST,encodeManagement(this.id,crypto.getRandomValues(new Uint8Array(16)),code,body)));if(value.code)throw Object.assign(Error('Device status '+value.code),{code:value.code});return value.body;}};
try{await exchange(FRAME.HELLO,new Uint8Array());await session.request(0,[]);const author=await proveDeviceAuthor(session);let offset=0,generation=null,total=0;const matching=[];
 for(let i=0;i<128;i++){const page=await readDeviceDraftPage(session,author,{offset,catalogGeneration:generation});total+=page.rows.length;for(const row of page.rows)if(row.name.startsWith('Wio网页验收-'))matching.push({id:Buffer.from(row.id).toString('hex'),name:row.name,generation:String(row.generation),publication:row.publication});if(!page.hasMore){console.log(JSON.stringify({author:author.fingerprint,total,matching}));break;}offset=page.nextOffset;generation=page.catalogGeneration;if(i===127)throw Error('Catalogue exceeded bounded scan');}
}finally{console.log(JSON.stringify({responses:hardware.responses}));await hardware.close();}
