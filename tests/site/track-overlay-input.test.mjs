import test from 'node:test';import assert from 'node:assert/strict';
import {overlayNumberInput} from '../../site/tracks/src/kml-overlay-input.js';
import {kmlOverlayEditorText} from '../../site/i18n/kml-overlay-editor-labels.js';
import {locales} from '../../site/i18n/registry.js';
test('overlay number input supports local decimals and typed native digit shapes without grouping removal',()=>{
 for(const [input,locale,expected] of [['30,25','de','30.25'],['30,25','fr','30.25'],['٣٠٫٢٥','ar','30.25'],['۳۰٫۲۵','ar','30.25'],['−１７９.５','ja','-179.5'],[' 1e-7 ','en','1e-7'],['1,000','en','1,000'],['1٬000','ar','1٬000'],['30,2,5','de','30.2.5']])assert.equal(overlayNumberInput(input,locale),expected);
});
test('all supported languages have complete and nonempty overlay editor labels',()=>{
 const en=kmlOverlayEditorText('en');for(const {id} of locales){const text=kmlOverlayEditorText(id);assert.deepEqual(Object.keys(text),Object.keys(en));assert.ok(Object.values(text).every(value=>typeof value==='string'&&value.length));if(id!=='en')assert.notEqual(text.title,en.title);}
});
