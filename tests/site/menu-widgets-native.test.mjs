import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {join} from 'node:path';
import {framePng} from './native-frame-png.mjs';

const boards = [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240],
  ['t-display-p4-tft',1168,540],['t-display-p4-amoled',1232,568]];
for (const [device,width,height] of boards.filter(([name])=>!process.env.WIDGET_UI_DEVICES || process.env.WIDGET_UI_DEVICES.split(',').includes(name))) {
  const base = new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create} = await import(`${base}.js`);
  const data = await readFile(new URL(`${base}.data`));
  let runtime, frame;
  runtime = await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
    onFrame(pointer,w,h){assert.equal(w,width);assert.equal(h,height);frame=Buffer.from(runtime.HEAPU8.subarray(pointer,pointer+w*h*2));},
    print(){},printErr(message){console.error(message);}});
  const tick = (ms=250) => {for(let elapsed=0;elapsed<ms;elapsed+=50)runtime._native_tick(50);};
  const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const key=text=>{runtime._native_key(typeof text==='string'?text.charCodeAt(0):text);tick();};
  const swipe=(x,y,dx,dy)=>{
    runtime._native_pointer(x,y,1);
    for(let step=1;step<=8;step++){runtime._native_pointer(x+Math.round(dx*step/8),y+Math.round(dy*step/8),1);runtime._native_tick(30);}
    runtime._native_pointer(x+dx,y+dy,0);tick();
  };
  const capture=async name=>{
    if(!process.env.WIDGET_UI_ARTIFACT_DIR)return;
    const directory=join(process.env.WIDGET_UI_ARTIFACT_DIR,device);
    await mkdir(directory,{recursive:true});
    await writeFile(join(directory,`${name}.png`),framePng(frame,width,height));
    await writeFile(join(directory,`${name}.json`),JSON.stringify(snapshot(),null,2));
  };
  const checkAgenda=()=>{
    const before=runtime._native_preview_agenda_queries();
    tick(5000);
    assert.equal(runtime._native_preview_agenda_queries(),before,'visible Agenda does not query on every frame');
    runtime._native_preview_agenda_state(0,1);tick(31000);
    assert.ok(snapshot().some(item=>item.text==='Storage unavailable'));
    runtime._native_preview_agenda_state(1,0);tick(31000);
    assert.ok(snapshot().some(item=>item.text==='Waiting for time'));
    runtime._native_preview_agenda_state(1,1);tick(31000);
    assert.ok(snapshot().some(item=>item.text.includes('Trail check-in')),'Agenda recovers from service unavailability');
  };
  const checkSkyplot=()=>{
    const satellites=snapshot().filter(item=>/^\d+$/.test(item.text)&&Number(item.text)>0&&Number(item.text)<=12);
    assert.equal(satellites.length,8,'eight ranked satellites');
    const xs=satellites.map(item=>item.x), ys=satellites.map(item=>item.y);
    assert.ok(Math.max(...xs)-Math.min(...xs)>25,'first-frame skyplot spreads satellites horizontally');
    assert.ok(Math.max(...ys)-Math.min(...ys)>25,'first-frame skyplot spreads satellites vertically');
  };
  runtime._native_init(device==='pager'?0:1);runtime._native_open_page(10);tick();
  const p4=device.startsWith('t-display');
  const map=snapshot().find(item=>item.button&&item.text==='Map');
  assert.ok(map,`${device}: Map launcher`);
  await capture('menu');
  if(p4) {
    checkSkyplot();
    assert.equal(runtime._native_widget_count(),4);
    assert.equal(runtime._native_widget_leases(),4);
    swipe(40,100,150,0);
    assert.equal(runtime._native_widget_page(),0,'left app area does not page widgets');
    assert.equal(runtime._native_widget_count(),4,'dragging an app icon must not launch it');
    swipe(width-100,Math.floor(height/2),-200,0);
    assert.equal(runtime._native_widget_page(),1,'right-side swipe pages widgets');
    assert.equal(runtime._native_widget_count(),1);
    assert.equal(runtime._native_widget_leases(),1);
    assert.ok(snapshot().some(item=>item.text.includes('Trail check-in')));
    assert.equal(runtime._native_dashboard_geometry(),0);
    await capture('agenda');
    checkAgenda();
    swipe(width-320,Math.floor(height/2),200,0);
    assert.equal(runtime._native_widget_page(),0);
    assert.equal(runtime._native_widget_count(),4);
  } else {
    const names=['Map','Sky Plot','Tracker','Chat','Contacts','Team','Geocaching','Calculator'];
    const icons=snapshot().filter(item=>item.button&&names.includes(item.text));
    const firstY=icons[0].y;
    const firstRow=icons.filter(item=>item.y===firstY).sort((a,b)=>a.x-b.x);
    const columns=firstRow.length;
    assert.equal(columns,Math.floor(width/72),`${device}: adaptive column count`);
    const left=firstRow[0].x, right=width-firstRow.at(-1).x-firstRow.at(-1).w;
    assert.ok(Math.abs(left-right)<=1,`${device}: symmetric outside margins`);
    assert.ok(firstY>=32,`${device}: focus is separated from top bar`);
    for(let column=1;column<columns;column++) {
      assert.equal(firstRow[column].w,firstRow[0].w,'uniform column widths');
      assert.equal(firstRow[column].x-firstRow[column-1].x-firstRow[column-1].w,6,'uniform column gaps');
    }
    for(const item of snapshot().filter(item=>item.button&&item.w===firstRow[0].w)) {
      assert.ok(firstRow.some(column=>column.x===item.x),'stable row tracks');
    }
    assert.equal(runtime._native_widget_page(),-1);
    assert.equal(runtime._native_widget_leases(),0,'closed feed owns no widget state');
    runtime._native_focus(map.id);
    const focused=runtime._native_menu_focus();
    runtime._native_encoder(1);tick();
    assert.notEqual(runtime._native_menu_focus(),focused,'menu encoder changes launcher focus');
    assert.equal(runtime._native_widget_page(),-1,'menu encoder never opens widgets');
    runtime._native_focus(map.id);
    if(device==='pager')key('v');else swipe(30,80,100,0);
    assert.equal(runtime._native_widget_page(),0,'open negative screen');
    assert.equal(runtime._native_widget_count(),1);
    key('h');
    key('s');runtime._native_encoder(1);tick();
    assert.equal(runtime._native_widget_page(),0,'modal help owns keyboard and encoder input');
    key('h');
    for(let page=0;page<6;page++){
      assert.equal(runtime._native_widget_page(),page);
      assert.equal(runtime._native_widget_count(),1,'one widget at a time');
      assert.equal(runtime._native_widget_leases(),1,'one independent PSRAM owner');
      assert.equal(runtime._native_dashboard_geometry(),0,`${device}: widget ${page} geometry`);
      if(page===1)checkSkyplot();
      assert.ok(!snapshot().some(item=>item.text==='Mini Compass'),'targets without a magnetometer omit Compass');
      await capture(`widget-${page}`);
      if(page===3) {
        assert.ok(snapshot().some(item=>item.text.includes('Trail check-in')),'Agenda reads the provided service port');
        checkAgenda();
      }
      if(page<5){if(device==='pager'){runtime._native_encoder(1);tick();}else swipe(width/2,height-65,0,-70);}
    }
    key('s');assert.equal(runtime._native_widget_page(),5,'bounded at final widget');
    runtime._native_encoder(-1);tick();assert.equal(runtime._native_widget_page(),4);
    key('w');assert.equal(runtime._native_widget_page(),3);
    key('w');assert.equal(runtime._native_widget_page(),2);
    const before=runtime._native_preview_agenda_queries();
    tick(35000);assert.equal(runtime._native_preview_agenda_queries(),before,'hidden Agenda is never queried');
    if(device==='pager')key('v');else swipe(width-30,80,-100,0);
    assert.equal(runtime._native_widget_page(),-1);
    assert.equal(runtime._native_widget_leases(),0);
    runtime._native_encoder(1);tick();
    assert.equal(runtime._native_widget_page(),-1,'encoder resumes launcher navigation');
    for(let cycle=0;cycle<50;cycle++){
      runtime._native_focus(map.id);key(device==='pager'?'v':'a');
      runtime._native_encoder(1);tick();key(device==='pager'?'v':'d');
      assert.equal(runtime._native_widget_leases(),0,`${device}: repeated feed exit frees state`);
    }
  }
  runtime._native_open_page(11);tick();
  assert.equal(runtime._native_widget_leases(),0,'entering an app frees all widget states');
  runtime._native_open_page(10);tick();
  assert.equal(runtime._native_widget_count(),p4?4:0);
  console.log(`${device}: adaptive menu, contextual encoder, widget ownership and navigation passed.`);
}
