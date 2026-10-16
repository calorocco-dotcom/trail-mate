import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

const boards = [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240],
  ['t-display-p4-tft',1168,540],['t-display-p4-amoled',1232,568]];
for (const [device,width,height] of boards) {
  const base = new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create} = await import(`${base}.js`);
  const data = await readFile(new URL(`${base}.data`));
  const runtime = await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
    print(){},printErr:console.error});
  const tick = (ms=100) => {for(let elapsed=0;elapsed<ms;elapsed+=10)runtime._native_tick(10);};
  const snapshot = () => JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const swipe = () => {
    const x=width-100,y=Math.floor(height/2);
    runtime._native_pointer(x,y,1);
    for(let step=1;step<=8;step++){
      runtime._native_pointer(x-Math.round(200*step/8),y,1);tick(30);
    }
    runtime._native_pointer(x-200,y,0);tick(300);
  };
  runtime._native_init(device==='pager'?0:1);
  runtime._native_preview_music_playing(1);
  runtime._native_open_page(10);tick(100);
  if(device.startsWith('t-display')) {
    swipe();swipe();
    assert.equal(runtime._native_widget_page(),2);
  } else {
    runtime._native_key('a'.charCodeAt(0));tick(300);
    for(let page=0;page<4;page++){runtime._native_encoder(1);tick(100);}
    assert.equal(runtime._native_widget_page(),4);
  }
  assert.equal(runtime._native_widget_leases(),1);
  assert.ok(snapshot().some(item=>item.text==='After the rain'));
  tick(150);
  // A service publication bypasses the widget click callback. This catches a
  // slow parent timer too, using the real production LVGL dashboard.
  for(let change=1;change<=5;change++) {
    const title=`Widget song ${change}`,artist=`Widget artist ${change}`,album=`Widget album ${change}`;
    runtime.ccall('native_preview_music_text',null,Array(6).fill('string'),[title,artist,album,'','','']);
    tick(100);
    const view=snapshot();
    for(const text of [title,artist,album])assert.ok(view.some(item=>item.text===text),`${device}: ${text} within 100ms`);
    assert.ok(!view.some(item=>item.text===`Widget song ${change-1}`),'old metadata is removed');
  }
  runtime._native_open_page(11);tick(300);
  assert.equal(runtime._native_widget_leases(),0,'leaving destroys the only observing widget');
  console.log(`${device}: asynchronous widget ID3 refresh within 100ms and observer teardown passed.`);
}
