import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {deflateSync} from 'node:zlib';

// RGB565 comes directly from the production LVGL renderer, without browser
// scaling or a second implementation of the device's layout.
function png(frame, width, height) {
  const crc32 = bytes => {
    let crc = -1;
    for (const byte of bytes) {
      crc ^= byte;
      for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ -1) >>> 0;
  };
  const chunk = (type, bytes) => {
    const result = Buffer.alloc(bytes.length + 12);
    result.writeUInt32BE(bytes.length);
    result.write(type, 4);
    bytes.copy(result, 8);
    result.writeUInt32BE(crc32(result.subarray(4, -4)), result.length - 4);
    return result;
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width); header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = 2;
  const pixels = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const value = frame.readUInt16LE((y * width + x) * 2);
    const offset = y * (width * 3 + 1) + 1 + x * 3;
    pixels[offset] = Math.round(((value >>> 11) & 31) * 255 / 31);
    pixels[offset + 1] = Math.round(((value >>> 5) & 63) * 255 / 63);
    pixels[offset + 2] = Math.round((value & 31) * 255 / 31);
  }
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),
    chunk('IHDR', header), chunk('IDAT', deflateSync(pixels)), chunk('IEND', Buffer.alloc(0))]);
}

const pages = [[10,'menu'],[5,'chat-list'],[6,'conversation'],[7,'compose'],
  [11,'settings'],[12,'contacts'],[13,'team'],[14,'tracker'],[15,'map'],
  [1,'sky-plot'],[3,'walkie'],[4,'sstv'],[8,'extensions'],[9,'network'],
  [16,'protocol-probe'],[0,'calculator'],[17,'shortcut-help'],[18,'power']];
for (const [device, width, height] of [['t-display-p4-tft',1168,540], ['t-display-p4-amoled',1232,568]]) {
  const base = new URL(`../../site/assets/native/${device}`, import.meta.url);
  const {default:create} = await import(`${base}.js`);
  const data = await readFile(new URL(`${base}.data`));
  let runtime, frame, frames = 0;
  runtime = await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
    onFrame(ptr,w,h){
      assert.equal(w,width); assert.equal(h,height);
      frame = Buffer.from(runtime.HEAPU8.subarray(ptr, ptr+w*h*2)); frames++;
    }, print(){}, printErr(message){console.error(message);}});
  runtime._native_init(0);
  const tick = () => {for(let i=0;i<5;i++) runtime._native_tick(50);};
  const snapshot = () => JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const directory = process.env.P4_UI_ARTIFACT_DIR && join(process.env.P4_UI_ARTIFACT_DIR,device);
  if(directory) await mkdir(directory,{recursive:true});
  const capture = async name => {
    tick();
    const labels=snapshot();
    assert.ok(labels.length>0,`${device}:${name}: visible page content`);
    assert.equal(frame.length,width*height*2);
    if(directory) {
      await writeFile(join(directory,`${name}.png`),png(frame,width,height));
      await writeFile(join(directory,`${name}.json`),JSON.stringify(labels,null,2));
    }
    return labels;
  };
  for(const [page,name] of pages) {
    runtime._native_open_page(page);
    await capture(name);
    if(page===10) assert.equal(runtime._native_dashboard_geometry(),0,`${device}: dashboard drawable geometry`);
  }
  const click = text => {
    const item=snapshot().findLast(value=>value.button && value.text===text);
    assert.ok(item,`${device}: ${text} exists`);
    const x=item.x+Math.floor(item.w/2), y=item.y+Math.floor(item.h/2);
    assert.ok(x>=0 && x<width && y>=0 && y<height,`${device}: ${text} touch center is visible`);
    runtime._native_pointer(x,y,1);runtime._native_tick(50);
    runtime._native_pointer(x,y,0);tick();
  };
  for(const [page,filter,name] of [[5,'Direct','chat'],[11,'Profile','settings'],[12,'Contacts','contacts'],
      [14,'Tracks','tracker'],[8,'Installed','extensions'],[9,'Fav','network']]) {
    runtime._native_open_page(page);tick();
    const menu=snapshot().find(item=>item.button && item.text==='Menu');
    assert.ok(menu && menu.x+menu.w>=width-12 && menu.y>=height-48,`${device}:${name}: bottom menu alignment`);
    assert.ok(!snapshot().some(item=>item.button && item.text===filter),`${device}:${name}: full-width page`);
    click('Menu');
    const item=snapshot().find(value=>value.button && value.text===filter);
    assert.ok(item && item.x>=0 && item.x+item.w<=width && item.y>=24 && item.y+item.h<=height,
      `${device}:${name}: popup fits the viewport`);
    await capture(`${name}-menu`);
    runtime._native_key(27);tick();
    assert.equal(runtime._native_page_value(),page,`${device}:${name}: ESC closes popup first`);
  }
  // Same nested chat back contract as Pager/Deck. Touch the actual top bar.
  runtime._native_open_page(7); tick();
  for(const expected of [6,5,10]) {
    const back=snapshot().find(item=>item.button && item.text==='\uf053');
    assert.ok(back,`${device}: touch back exists`);
    assert.ok(back.x>=0 && back.y>=0 && back.x+back.w<=width && back.y+back.h<=height);
    runtime._native_pointer(back.x+Math.floor(back.w/2),back.y+Math.floor(back.h/2),1);
    runtime._native_tick(50);
    runtime._native_pointer(back.x+Math.floor(back.w/2),back.y+Math.floor(back.h/2),0);
    tick();
    assert.equal(runtime._native_page_value(),expected,`${device}: chat navigation depth`);
  }
  runtime._native_open_page(10); tick();
  assert.equal(runtime._native_dashboard_lifecycle(250),0,`${device}: dashboard deletion/rebuild must own its timer`);
  for(let angle=0;angle<360;angle+=15) {
    runtime._native_preview_heading(angle,1);
    runtime._native_preview_gps_fix(angle%30===0?1:0);
    runtime._native_tick(1100);
    assert.equal(runtime._native_dashboard_geometry(),0,`${device}: compass geometry at ${angle} degrees`);
  }
  runtime._native_open_page(0); tick();
  const hiddenTick=runtime._native_dashboard_tick();
  runtime._native_tick(5000);
  assert.equal(runtime._native_dashboard_tick(),hiddenTick,`${device}: hidden dashboard must not refresh`);
  runtime._native_open_page(10); tick();
  await capture('dashboard');
  console.log(`${device}: ${pages.length} real LVGL layouts and nested touch back passed (${frames} frames).`);
}
