import test from 'node:test';
import assert from 'node:assert/strict';
import {registerGeocachingNetwork,getGeocachingNetwork} from '../../site/geocaching/src/web-network.js';
test('maintenance shares browser IP worker and preserves device-signed content',async()=>{
 const calls=[];let online=false;
 const dispose=registerGeocachingNetwork({isReady:()=>online,rpc:async(command,args)=>{calls.push({command,args});return 'reply';}});
 const bridge=getGeocachingNetwork();assert.equal(bridge.ready(),false);await assert.rejects(bridge.directories());assert.equal(calls.length,0);
 online=true;assert.equal(await bridge.directories(),'reply');
 const signed=[new Uint8Array([1]),new Uint8Array(64)],author=new Uint8Array(64);
 assert.equal(await bridge.publish(signed,author,'target'),'reply');
 assert.equal(calls[1].command,'publish');assert.equal(calls[1].args.signed,signed);assert.equal(calls[1].args.authorPublicKey,author);
 assert.equal(await bridge.publicationReceipt(signed,author,'target'),'reply');assert.equal(calls[2].command,'publicationReceipt');assert.equal(calls[2].args.directoryKey,'target');
 online=false;await assert.rejects(bridge.publicationReceipt(signed,author,'target'));assert.equal(calls.length,3);
 dispose();assert.equal(getGeocachingNetwork(),undefined);
});
test('old registration cleanup cannot remove replacement; unconfirmed errors survive',async()=>{
 const first=registerGeocachingNetwork({isReady:()=>true,rpc:async()=>1});
 const error=Object.assign(Error('Lost reply'),{code:'PUBLICATION_UNCONFIRMED'});
 const second=registerGeocachingNetwork({isReady:()=>true,rpc:async()=>{throw error;}});first();
 await assert.rejects(getGeocachingNetwork().publish([],new Uint8Array(64),'target'),e=>e===error);second();
});
