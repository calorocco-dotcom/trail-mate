import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

const boards = [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240],
  ['t-display-p4-tft',1168,540],['t-display-p4-amoled',1232,568]];
for (const [device,width,height] of boards.filter(([name]) => !process.env.WIDGET_UI_DEVICES || process.env.WIDGET_UI_DEVICES.split(',').includes(name))) {
  const base = new URL(`../../site/assets/native/${device}`, import.meta.url);
  const {default:create} = await import(`${base}.js`);
  const data = await readFile(new URL(`${base}.data`));
  const runtime = await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
    print(){},printErr:console.error});
  const tick = (ms=250) => {for(let elapsed=0;elapsed<ms;elapsed+=20)runtime._native_tick(20);};
  const snapshot = () => JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const point = (x,y,down) => {runtime._native_pointer(x,y,down);tick(40);};
  runtime._native_init(device==='pager'?0:1);
  runtime._native_open_page(10);tick();
  const p4 = device.startsWith('t-display');
  const map = snapshot().find(item => item.button && item.text==='Map');
  assert.ok(map);
  if (p4) {
    const label = snapshot().find(item => item.text && item.x>width/2 && item.y>60 && item.y<height-60);
    assert.ok(label);
    const x=width-110,y=Math.floor(height/2);
    point(x,y,1);point(x-80,y,1);
    const moved = snapshot().find(item => item.id===label.id);
    assert.ok(moved && Math.abs(moved.x-(label.x-80))<=1,'dashboard follows the held finger before release');
    assert.equal(runtime._native_widget_page(),0,'dragging does not immediately replace the page');
    assert.equal(runtime._native_widget_leases(),4,'no incoming widget is preloaded during a drag');
    // Reverse before releasing: a short/cancelled gesture snaps back.
    point(x-15,y,1);point(x-15,y,0);tick();
    assert.equal(runtime._native_widget_page(),0);
    assert.equal(snapshot().find(item=>item.id===label.id).x,label.x);
    point(x,y,1);point(x-180,y,1);point(x-180,y,0);tick();
    assert.equal(runtime._native_widget_page(),1);
    assert.equal(runtime._native_widget_leases(),1);
  } else {
    const x=30,y=80;
    point(x,y,1);point(x+60,y,1);
    const moved = snapshot().find(item=>item.id===map.id);
    assert.ok(moved && Math.abs(moved.x-(map.x+60))<=1,'menu follows the held finger');
    assert.equal(runtime._native_widget_leases(),1,'only the revealed widget exists');
    point(x+15,y,1);point(x+15,y,0);tick();
    assert.equal(runtime._native_widget_page(),-1,'reversed drag returns to the menu');
    assert.equal(runtime._native_widget_leases(),0,'cancelled reveal frees the widget');
    assert.equal(snapshot().find(item=>item.id===map.id).x,map.x);
    point(x,y,1);point(x+110,y,1);tick(700);
    assert.equal(runtime._native_widget_leases(),1,'holding a drag does not launch the touched icon');
    point(x+110,y,0);tick();
    assert.equal(runtime._native_widget_page(),0);
    point(width/2,height-65,1);point(width/2,height-135,1);
    assert.equal(runtime._native_widget_page(),0,'vertical drag keeps its current widget until released');
    point(width/2,height-135,0);tick();
    assert.equal(runtime._native_widget_page(),1);
    assert.equal(runtime._native_widget_leases(),1);
    point(width-30,80,1);point(width-130,80,1);point(width-130,80,0);tick();
    assert.equal(runtime._native_widget_page(),-1);
    assert.equal(runtime._native_widget_leases(),0);
    // Teardown while a reveal is still animating must cancel its callback and timer.
    point(x,y,1);point(x+100,y,1);point(x+100,y,0);
    runtime._native_open_page(11);tick(1000);
    assert.equal(runtime._native_widget_leases(),0,'leaving mid-animation releases every view');
    runtime._native_open_page(10);tick();
    assert.equal(runtime._native_widget_page(),-1);
    assert.equal(snapshot().find(item=>item.button&&item.text==='Map').x,map.x);
  }
  runtime._native_open_page(11);tick(1000);
  assert.equal(runtime._native_widget_leases(),0,'page exit releases widgets and animation ownership');
  console.log(`${device}: finger tracking, snap-back, release commit and transition teardown passed.`);
}
