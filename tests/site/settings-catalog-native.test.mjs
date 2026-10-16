import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

for (const [device,width,height] of [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240],
  ['t-display-p4-tft',1168,540],['t-display-p4-amoled',1232,568]]) {
  const base = new URL(`../../site/assets/native/${device}`, import.meta.url);
  const {default:create} = await import(`${base}.js`);
  const data = await readFile(new URL(`${base}.data`));
  const runtime = await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
    print(){}, printErr:console.error});
  const tick = () => {for(let i=0;i<5;i++) runtime._native_tick(50);};
  const snapshot = () => JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const find = text => {
    let item = snapshot().findLast(item=>item.button && item.text===text);
    for(let attempt=0;!item && attempt<20;attempt++) {
      if(device==='pager') runtime._native_encoder(1);
      else {
        const x=Math.floor(width/2), bottom=height-52, top=65;
        runtime._native_pointer(x,bottom,1);runtime._native_tick(30);
        for(let step=1;step<=6;step++) {
          runtime._native_pointer(x,Math.round(bottom+(top-bottom)*step/6),1);
          runtime._native_tick(30);
        }
        runtime._native_pointer(x,top,0);
      }
      tick();
      item=snapshot().findLast(item=>item.button && item.text===text);
    }
    assert.ok(item, `${device}: ${text} exists; page=${runtime._native_page_value()} labels=${snapshot().filter(item=>item.button).map(item=>item.text).join('|')}`);
    return item;
  };
  const click = text => {
    const item=find(text);
    runtime._native_activate(item.id); tick();
  };
  runtime._native_init(device==='pager'?0:1);
  for (let cycle=0;cycle<3;cycle++) {
    runtime._native_open_page(11); tick();
    click('Menu');
    const categories = snapshot().filter(item=>item.button &&
      ['Profile','Mesh','Radio','Wi-Fi','Reticulum','Location','Device','Maintenance'].includes(item.text))
      .map(item=>item.text).filter((item,index,items)=>items.indexOf(item)===index);
    runtime._native_key(27); tick();
    for(const category of categories) {
      click('Menu'); click(category);
      assert.ok(snapshot().filter(item=>item.button).length>2, `${device}: ${category} has bound items`);
    }
    click('Menu'); click('Device');
    click('Display Language');
    assert.ok(snapshot().some(item=>item.button && item.text==='English'), `${device}: dynamic locale choices`);
    runtime._native_key(27); tick();
    click('Time Zone');
    assert.ok(snapshot().some(item=>item.button && item.text==='UTC'), `${device}: dynamic timezone choices`);
    click('UTC');
    // Persisting time settings may reconstruct the page through its normal
    // configuration notification. Re-enter the category and verify bindings.
    click('Menu'); click('Device');
    find('Time Zone');
    assert.ok(snapshot().some(item=>item.button && item.text==='UTC'), `${device}: selected timezone projection`);
    click('Time Zone'); click('Beijing (UTC+8)');
    runtime._native_open_page(10); tick();
  }
  console.log(`${device}: immutable settings catalogue, all visible categories and dynamic options survived repeated visits.`);
}
