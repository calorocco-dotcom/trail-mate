import test from 'node:test';
import assert from 'node:assert/strict';
import {validateTrackEditorMetadata,draftTrackEditorMetadata} from '../../site/tracks/src/track-editor-metadata.js';
import {trackMetadataText} from '../../site/i18n/track-metadata-labels.js';
import {locales} from '../../site/i18n/registry.js';
import {encode} from '../../site/geocaching/src/protocol.js';
const manifest=()=>[2,new Uint8Array(64).fill(1),new Uint8Array(16).fill(1),0,1,null,0,1,1,'Route','',[0,0,1,1],[1,null,null,2],[new Uint8Array(32),123,'application/vnd.google-earth.kml+xml',1],[250,['原标签','trail, ridge'],0,1,0,0,0,null],null];
test('editor preserves future activity values, exact tags and UTF-8 limits without borrowing caller arrays',()=>{
 const input={activity:250,tags:['山'.repeat(16),'trail, ridge']},frozen=validateTrackEditorMetadata(input);input.tags[0]='changed';
 assert.equal(frozen.tags[0],'山'.repeat(16));assert.equal(frozen.activity,250);
 for(const value of [{activity:256,tags:[]},{activity:true,tags:[]},{activity:0,tags:['山'.repeat(17)]},{activity:0,tags:['same','same']},{activity:0,tags:['  ']},{activity:0,tags:['line\nbreak']},{activity:0,tags:Array.from({length:9},(_,i)=>String(i))},{activity:0,tags:[],extra:true}])assert.throws(()=>validateTrackEditorMetadata(value),error=>error.code==='DRAFT_METADATA_INVALID');
});
test('signed source and predecessor supply metadata, explicit draft edits override it, legacy drafts remain unspecified',()=>{
 const signed=encode([encode(manifest()),new Uint8Array(64)]).buffer;
 assert.deepEqual(draftTrackEditorMetadata({sourceProof:signed}),{activity:250,tags:['原标签','trail, ridge']});
 assert.deepEqual(draftTrackEditorMetadata({predecessor:signed,editorMetadata:{activity:3,tags:['new']}}),{activity:3,tags:['new']});
 assert.deepEqual(draftTrackEditorMetadata({}),{activity:0,tags:[]});
 const bad=manifest();bad[14][1]=['x','x'];assert.throws(()=>draftTrackEditorMetadata({sourceProof:encode([encode(bad),new Uint8Array(64)]).buffer}));
});
test('all website locales define the same activity IDs and complete editing copy',()=>{
 for(const {id} of locales){const text=trackMetadataText(id);assert.equal(text.activities.length,9);assert.ok(text.unknown.includes('{value}'));for(const value of Object.values(text))assert.ok(Array.isArray(value)||typeof value==='string'&&value.trim());}
});
