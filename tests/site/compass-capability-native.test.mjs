import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

for (const device of ['pager','tdeck','wio-tracker-l2','t-display-p4-tft','t-display-p4-amoled']) {
  const p4 = device.startsWith('t-display');
  const base = new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create} = await import(`${base}.js`);
  const data = await readFile(new URL(`${base}.data`));
  for (const supported of p4 ? [true,false] : [false]) {
    let runtime;
    runtime = await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
      getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
      onFrame(){},print(){},printErr:console.error});
    runtime._native_preview_magnetometer(supported?1:0);
    runtime._native_preview_heading(0,0); // No sample yet must not hide supported hardware.
    assert.equal(runtime._native_preview_heading_leases(),0);
    runtime._native_init(device==='pager'?0:1);
    runtime._native_open_page(10);
    const tick=()=>{for(let i=0;i<8;i++)runtime._native_tick(50);};
    const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
    const compass=()=>snapshot().some(item=>item.text==='Mini Compass');
    const click=text=>{const item=snapshot().find(item=>item.button&&item.text===text);assert.ok(item,`${device}: ${text}`);runtime._native_activate(item.id);tick();};
    tick();
    if(p4) {
      assert.equal(compass(),supported,'magnetometer capability, independent of heading readiness');
      assert.equal(runtime._native_widget_count(),supported?4:3,'no empty Compass slot');
      assert.equal(runtime._native_preview_heading_leases(),supported?1:0);
      assert.equal(runtime._native_dashboard_geometry(),0);
      click('\uf054');
      assert.equal(runtime._native_preview_heading_leases(),0,'leaving overview releases heading');
      assert.ok(snapshot().some(item=>item.text==='Agenda'),'filtered overview keeps Agenda as next page');
    } else {
      const map=snapshot().find(item=>item.button&&item.text==='Map');assert.ok(map);
      runtime._native_focus(map.id);runtime._native_key('a'.charCodeAt(0));tick();
      for(let page=0;page<6;page++) {
        assert.equal(runtime._native_widget_page(),page);
        assert.ok(!compass(),'absent magnetometer never creates Compass');
        assert.equal(runtime._native_preview_heading_leases(),0,'capability filtering never acquires hardware');
        assert.ok(snapshot().some(item=>item.text===`${page+1} / 6`));
        if(page<5){runtime._native_encoder(1);tick();}
      }
      runtime._native_encoder(1);tick();assert.equal(runtime._native_widget_page(),5);
    }
    runtime._native_open_page(11);tick();
    assert.equal(runtime._native_preview_heading_leases(),0);
    assert.equal(runtime._native_widget_leases(),0);
  }
  console.log(`${device}: magnetometer-gated Compass, filtered pages and heading leases passed.`);
}
