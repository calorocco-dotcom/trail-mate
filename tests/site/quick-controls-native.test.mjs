import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {join} from 'node:path';
import {framePng} from './native-frame-png.mjs';
const boards=[['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240],['t-display-p4-tft',1168,540],['t-display-p4-amoled',1232,568]];
for(const [device,width,height] of boards.filter(([name])=>!process.env.WIDGET_UI_DEVICES||process.env.WIDGET_UI_DEVICES.split(',').includes(name))){
 const base=new URL(`../../site/assets/native/${device}`,import.meta.url);
 const {default:create}=await import(`${base}.js`);const data=await readFile(new URL(`${base}.data`));let runtime,frame;
 runtime=await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
 onFrame(ptr,w,h){assert.equal(w,width);assert.equal(h,height);frame=Buffer.from(runtime.HEAPU8.subarray(ptr,ptr+w*h*2));},print(){},printErr:console.error});
 const tick=()=>{for(let i=0;i<6;i++)runtime._native_tick(50);};
 const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
 const key=k=>{runtime._native_key(typeof k==='string'?k.charCodeAt(0):k);tick();};
 const click=text=>{const item=snapshot().find(item=>item.button&&item.text===text);assert.ok(item,`${device}: ${text}`);runtime._native_activate(item.id);tick();};
 const capture=async name=>{if(!process.env.WIDGET_UI_ARTIFACT_DIR)return;const dir=join(process.env.WIDGET_UI_ARTIFACT_DIR,device);await mkdir(dir,{recursive:true});await writeFile(join(dir,`${name}.png`),framePng(frame,width,height));await writeFile(join(dir,`${name}.json`),JSON.stringify(snapshot(),null,2));};
 runtime._native_init(device==='pager'?0:1);runtime._native_preview_music_playing(1);runtime._native_open_page(10);tick();
 const p4=device.startsWith('t-display');
 if(p4){click('\uf054');click('\uf054');}else{
  const map=snapshot().find(item=>item.button&&item.text==='Map');runtime._native_focus(map.id);key('a');
  for(let i=0;i<5;i++){runtime._native_encoder(1);tick();}
 }
 assert.ok(snapshot().some(item=>item.text==='Music'));assert.ok(snapshot().some(item=>item.text==='After the rain'));
 await capture('music-widget');
 click('Pause');assert.ok(snapshot().some(item=>item.text==='Play'));click('Play');
 click('Repeat all');assert.ok(snapshot().some(item=>item.text==='Repeat one'));
 click('\uf051');assert.ok(snapshot().some(item=>item.text==='Pines at dusk'));
 if(p4)click('\uf054');else{runtime._native_encoder(1);tick();}
 assert.ok(snapshot().some(item=>item.text==='Quick controls'));
 for(const text of ['Wi-Fi','LoRa','GPS','Volume','Brightness','35%','70%'])assert.ok(snapshot().some(item=>item.text===text),`${device}: ${text}`);
 assert.equal(runtime._native_dashboard_geometry(),0);
 await capture('quick-controls');
 if(device==='pager'){
  key('1');key('2');key('3');key('+');key(']');
  assert.ok(snapshot().some(item=>item.text==='40%'));assert.ok(snapshot().some(item=>item.text==='75%'));
  assert.equal(runtime._native_widget_page(),6,'quick keys do not change page');
 }else{
  click('Wi-Fi');click('GPS');
 }
 await capture('quick-controls-changed');
 const pageBefore=runtime._native_widget_page();
 const slider=snapshot().filter(item=>item.slider)[0];assert.ok(slider,'visible volume slider');
 const drag=(dx,dy)=>{
   const current=snapshot().filter(item=>item.slider)[0];
   const x=current.x+Math.round((current.w-1)*current.value/100),y=current.y+current.h/2;
   runtime._native_pointer(x,y,1);runtime._native_tick(50);
   for(let step=1;step<=8;step++){runtime._native_pointer(Math.round(x+dx*step/8),Math.round(y+dy*step/8),1);runtime._native_tick(40);}
   runtime._native_pointer(Math.round(x+dx),Math.round(y+dy),0);tick();
 };
 drag(Math.min(100,slider.w/3),0);
 assert.equal(runtime._native_widget_page(),pageBefore,'horizontal slider drag retains the current widget');
 drag(0,-80);
 assert.equal(runtime._native_widget_page(),pageBefore,'vertical slider drag retains the current widget');
 if(device==='pager'){
   runtime._native_encoder(-1);tick();key(10); // LV_KEY_ENTER is '\n'.
   assert.ok(snapshot().some(item=>item.text==='Now Playing'),'Enter on music widget opens the full player');
   assert.equal(runtime._native_widget_count(),0,'opening music releases the widget');
   assert.equal(runtime._native_widget_leases(),1,'only the full music page retains a PSRAM UI lease');
 }
 runtime._native_open_page(11);tick();assert.equal(runtime._native_widget_leases(),0,'leaving releases widget PSRAM state and artwork');
 console.log(`${device}: quick controls, playback widget, mode and PSRAM lifetime passed.`);
}
