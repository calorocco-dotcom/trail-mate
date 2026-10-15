import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

// Exercise real page sources compiled to WASM. The small LVGL contract test
// separately checks shared-menu object lifetime and activation semantics.
for(const [device,width,height] of [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240]]) {
  const base=new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create}=await import(`${base}.js`);
  const bytes=await readFile(new URL(`${base}.data`));
  const runtime=await create({
    wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);},
    print(){},printErr(){},
  });
  runtime._native_init(device==='pager'?0:1);
  const tick=()=>{for(let i=0;i<5;i++)runtime._native_tick(50);};
  const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const click=text=>{
    const button=snapshot().findLast(item=>item.button&&item.text===text);
    assert.ok(button,`${device}: missing ${text}`);
    const x=button.x+Math.floor(button.w/2),y=button.y+Math.floor(button.h/2);
    runtime._native_pointer(x,y,1);runtime._native_tick(50);
    runtime._native_pointer(x,y,0);tick();
  };
  for(const [page,filterText,contentText] of [[5,'Direct','Alex'],[12,'Contacts','No nodes'],[14,'Tracks','Not recording'],[11,'Profile'],[8,'Installed'],[9,'Fav']]) {
    runtime._native_open_page(page);tick();
    const menu=snapshot().find(item=>item.button&&item.text==='Menu');
    assert.ok(menu,`${device}:${page}: bottom menu exists`);
    assert.ok(menu.y>=height-48&&menu.x+menu.w>=width-12,`${device}:${page}: menu is in the bottom-right corner`);
    assert.ok(!snapshot().some(item=>item.button&&item.text===filterText),`${device}:${page}: filters do not consume a permanent column`);
    if(contentText) {
      const content=snapshot().find(item=>!item.button&&item.text===contentText);
      assert.ok(content&&content.x<width/4,`${device}:${page}: content uses the recovered left-hand space`);
    }
    click('Menu');
    const filter=snapshot().find(item=>item.button&&item.text===filterText);
    assert.ok(filter&&filter.x>=0&&filter.x+filter.w<=width,`${device}:${page}: filter menu fits the viewport`);
    assert.ok(filter.y<menu.y,`${device}:${page}: menu opens upward`);
    if(page===5||page===12) {
      click('Info');
      assert.ok(snapshot().some(item=>item.button&&item.text==='Back'),`${device}:${page}: radio Info has a touch return action`);
      click('Back');
      assert.ok(snapshot().some(item=>item.button&&item.text===filterText));
    }
    runtime._native_key(27);tick();
    assert.equal(runtime._native_page_value(),page,`${device}:${page}: Escape closes the menu before leaving the page`);
    assert.ok(!snapshot().some(item=>item.button&&item.text===filterText),`${device}:${page}: Escape hides the filter menu`);
  }
  console.log(`${device}: six former two-pane pages use bottom menus, full-width content and menu-first back navigation.`);
}
