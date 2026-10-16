import test from 'node:test';
import assert from 'node:assert/strict';
import {trackId,parseTrackReference,trackShareLink,copyTrackReference} from '../../site/tracks/src/shared-id.js';
const id=Uint8Array.from({length:32},(_,i)=>i),revision=new Uint8Array(32).fill(9);
test('stable IDs copy full bytes and share links retain Pages prefix and exact revision',()=>{
 const link=trackShareLink('https://example.github.io/trail-mate/',id,{revision});
 assert.ok(link.startsWith('https://example.github.io/trail-mate/tracks/?id=tmtrk1_'));
 assert.deepEqual(parseTrackReference(link),{id,revision});assert.deepEqual(parseTrackReference(trackId(id)),{id,revision:null});
 assert.deepEqual(parseTrackReference('?id='+trackId(id),{baseUrl:'https://example.org/tracks/'}).id,id);
});
test('partial IDs, duplicate parameters and executable URLs are rejected',()=>{
 for(const text of ['tmtrk1_abcd','javascript:alert(1)','https://example.org/?id='+trackId(id)+'&id='+trackId(id),'https://example.org/?id='+trackId(id)+'&rev=short'])assert.throws(()=>parseTrackReference(text));
});
test('clipboard denial provides manual copy text rather than claiming success',async()=>{
 const text=trackId(id);let written;
 assert.equal((await copyTrackReference(text,{clipboard:{async writeText(value){written=value}}})).copied,true);assert.equal(written,text);
 assert.deepEqual(await copyTrackReference(text,{clipboard:{async writeText(){throw Error('Denied')}}}),{copied:false,text});
});
