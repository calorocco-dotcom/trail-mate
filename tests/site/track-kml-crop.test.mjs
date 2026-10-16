import test from 'node:test';import assert from 'node:assert/strict';import {readKmlIconCrop,resolveKmlIconCrop,validateKmlIconCrop} from '../../site/tracks/src/kml-icon-crop.js';import {kmlCropText} from '../../site/i18n/kml-crop-labels.js';import {locales} from '../../site/i18n/registry.js';
const image={width:128,height:64},icon=fields=>({children:fields.map(([localName,textContent])=>({namespaceURI:'http://www.google.com/kml/ext/2.2',localName,textContent}))});
test('palette defaults and lower-left offsets produce exact raster rectangles',()=>{
 assert.equal(readKmlIconCrop(null),null);assert.equal(readKmlIconCrop(icon([])),null);assert.deepEqual(resolveKmlIconCrop(null,image),{x:0,y:0,width:128,height:64,top:0});
 const crop=readKmlIconCrop(icon([['x','64'],['y','0'],['w','64'],['h','32']]));assert.deepEqual(resolveKmlIconCrop(crop,image),{x:64,y:0,width:64,height:32,top:32});assert.equal(resolveKmlIconCrop({...crop,y:32},image).top,0);
 assert.deepEqual(resolveKmlIconCrop(readKmlIconCrop(icon([['w','32'],['h','16']])),image),{x:0,y:0,width:32,height:16,top:48});
});
test('strict integer syntax, duplicates, unsupported values and out-of-image bounds are rejected',()=>{
 for(const fields of [[['x','-1']],[['x','1.5']],[['w','0']],[['w','-2']],[['x','1e2']],[['w','2147483648']],[['x','0'],['x','1']],[['h','NaN']]])assert.throws(()=>readKmlIconCrop(icon(fields)),{code:'KML_ICON_CROP'});
 for(const crop of [{x:96,y:0,w:64,h:32},{x:0,y:48,w:32,h:32},{x:1,y:0,w:-1,h:-1}])assert.throws(()=>resolveKmlIconCrop(crop,image),{code:'KML_ICON_CROP'});
 assert.throws(()=>validateKmlIconCrop({x:0,y:0,w:1.5,h:1}),{code:'KML_ICON_CROP'});
 assert.throws(()=>resolveKmlIconCrop(null,{width:NaN,height:64}),{code:'KML_ICON_CROP'});
});
test('non-gx names cannot change crop interpretation and all language errors are present',()=>{
 const foreign=icon([['x','-1']]);foreign.children[0].namespaceURI='http://www.opengis.net/kml/2.2';assert.equal(readKmlIconCrop(foreign),null);
 for(const {id} of locales)assert.ok(kmlCropText(id)?.trim());
});
