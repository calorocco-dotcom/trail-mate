import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {framePng} from './native-frame-png.mjs';

const base=new URL('../../site/assets/native/pager',import.meta.url);
const {default:create}=await import(`${base}.js`);
const data=await readFile(new URL(`${base}.data`));
let runtime,frame;
runtime=await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
  getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
  onFrame(ptr,w,h){frame=Buffer.from(runtime.HEAPU8.subarray(ptr,ptr+w*h*2));},print(){},printErr:console.error});
const tick=(ms=250)=>{for(let i=0;i<ms;i+=20)runtime._native_tick(20);};
const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
const has=text=>snapshot().some(item=>item.text===text);
const key=k=>{const code=typeof k==='string'?k.charCodeAt(0):k;runtime._native_keyboard_input(code,1);tick(20);runtime._native_keyboard_input(code,0);tick();};
const wheel=delta=>{runtime._native_encoder_input(delta,0);tick();};
const press=()=>{runtime._native_encoder_input(0,1);tick(30);runtime._native_encoder_input(0,0);tick();};
const capture=async name=>{
  if(!process.env.WIDGET_UI_ARTIFACT_DIR)return;
  const dir=join(process.env.WIDGET_UI_ARTIFACT_DIR,'pager');await mkdir(dir,{recursive:true});
  await writeFile(join(dir,`${name}.png`),framePng(frame,480,222));
};
runtime._native_init(0);runtime._native_open_page(10);tick();
const map=snapshot().find(item=>item.button&&item.text==='Map');
runtime._native_focus(map.id);
for(const k of ['a','w','s','d','A','W','S','D']){
  key(k);assert.equal(runtime._native_widget_page(),-1,'WASD never opens widgets at an edge');
}
const menuFocus=runtime._native_menu_focus();
key('v');assert.equal(runtime._native_widget_page(),0);
key('V');assert.equal(runtime._native_widget_page(),-1);
assert.equal(runtime._native_menu_focus(),menuFocus,'V preserves launcher selection');
runtime._native_preview_music_playing(1);
key('v');for(let i=0;i<4;i++)wheel(1);
assert.ok(has('Music')&&has('Pause'));
assert.ok(snapshot().some(item=>item.button&&item.text==='Pause'&&item.focused),'visible initial Play/Pause selection');
key(10);assert.ok(has('Play'),'keyboard Enter toggles pause exactly once');
press();assert.ok(has('Pause'),'encoder press toggles play exactly once');
key('d');key(10);assert.ok(has('Pines at dusk'),'select and activate Next');
key('a');key('a');key(10);assert.ok(has('After the rain'),'select and activate Previous');
key('d');key('d');key('d');press();assert.ok(has('Repeat one'),'encoder activates play mode');
await capture('music-keyboard-focus');
wheel(1);assert.ok(has('Quick controls'));
const state=name=>{const id=snapshot().find(item=>item.button&&item.text===name)?.id;return snapshot().find(item=>item.id===id&&['On','Off'].includes(item.text))?.text;};
assert.equal(state('Wi-Fi'),'Off');key(10);assert.equal(state('Wi-Fi'),'On');
key('d');press();assert.equal(state('LoRa'),'Off');
key('d');key(10);assert.equal(state('GPS'),'Off');
key('d');key(10);assert.ok(snapshot().some(item=>item.text?.includes('A/D adjust')));
const volume=snapshot().find(item=>item.slider).value;
key('d');assert.equal(snapshot().find(item=>item.slider).value,volume+5);
tick(1500);assert.equal(snapshot().find(item=>item.slider).value,volume+5,'cached refresh preserves applied volume');
key('a');key(10);key('d');press();
const brightness=snapshot().filter(item=>item.slider)[1].value;
key('d');assert.equal(snapshot().filter(item=>item.slider)[1].value,brightness+5);
key('a');press();
await capture('controls-keyboard-focus');
key('h');assert.ok(has('Select widget control'));
const page=runtime._native_widget_page();wheel(-1);key('d');assert.equal(runtime._native_widget_page(),page,'Help owns input');
key('h');wheel(-1);assert.equal(runtime._native_widget_page(),4);
// Open is a real selectable widget control; ownership includes a held press
// and its release after the app has replaced the widget.
for(let i=0;i<3;i++)key('d');
runtime._native_encoder_input(0,1);tick(250);
runtime._native_encoder_input(0,1);tick(250);
runtime._native_encoder_input(0,0);tick();
assert.equal(runtime._native_page_value(),19);
assert.ok(has('Now Playing'));
assert.equal(runtime._native_widget_count(),0);
const next=snapshot().find(item=>item.button&&item.text==='\uf051');assert.ok(next);
runtime._native_focus(next.id);
const selectedTrack=snapshot().find(item=>item.text==='After the rain')?.text;
press();tick(600);
assert.equal(runtime._native_input_focus(),next.id,'skip does not rebuild controls or move rotary focus');
press();tick(600);
assert.equal(runtime._native_input_focus(),next.id,'repeated skips retain the same control');
assert.ok(!selectedTrack||!has(selectedTrack),'ID3 changes while the controls remain alive');

const pages=[[0,'calculator'],[1,'sky-plot'],[2,'USB'],[3,'walkie'],[4,'SSTV'],[5,'chat-list'],
  [6,'conversation'],[7,'compose'],[8,'extensions'],[9,'network'],[11,'settings'],[12,'contacts'],
  [13,'team'],[14,'tracker'],[15,'map'],[16,'probe'],[19,'music'],[20,'geocaching'],[21,'agenda']];
for(const [id,name] of pages){
  runtime._native_open_page(id);tick();
  assert.ok(has('H Help'),`${name}: visible Help hint`);
  assert.ok(runtime._native_input_focus(),`${name}: initial focus exists`);
  const before=runtime._native_input_focus(),count=runtime._native_input_count();
  assert.equal(runtime._native_input_editing(),0,`${name}: starts in navigation mode`);
  wheel(1);
  if(count>1)assert.notEqual(runtime._native_input_focus(),before,`${name}: actual encoder moves focus`);
  // H is ordinary text inside an editor. Move to a button for page help.
  for(let n=0;n<count&&runtime._native_input_is_editor();n++)wheel(1);
  const focused=runtime._native_input_focus();
  key('h');
  assert.ok(snapshot().some(item=>item.text==='Select control'||(/Help$/.test(item.text)&&item.text!=='H Help')),`${name}: Help opens: ${snapshot().map(x=>x.text).join(' | ')}`);
  await capture(`help-${name}`);
  const shouldScroll=runtime._native_input_editing()&&runtime._native_input_scroll_bottom()>0;
  const scrollBefore=runtime._native_input_scroll_y();
  wheel(1);
  if(shouldScroll)assert.ok(runtime._native_input_scroll_y()>scrollBefore,`${name}: real encoder scrolls Help content`);
  key('h');
  assert.equal(runtime._native_page_value(),id,`${name}: Help closes without leaving app`);
  assert.equal(runtime._native_input_focus(),focused,`${name}: Help restores exact focus`);
  await capture(`input-${name}`);
}
// Empty Tracker must still expose Menu and Back, including after refresh.
runtime._native_open_page(14);tick();wheel(1);
const trackerFocus=runtime._native_input_focus();tick(6000);
assert.equal(runtime._native_input_focus(),trackerFocus,'periodic refresh retains selection');
key('m');tick();assert.ok(has('Tracks')&&has('Routes'));
key(10);tick();assert.ok(has('View')&&has('New')&&has('Current'),'keypad Enter opens the Tracks submenu exactly once');
key('m');tick();
const selectMenu=()=>{const menu=snapshot().find(item=>item.button&&item.text==='Menu');assert.ok(menu);runtime._native_focus(menu.id);};
selectMenu();key(10);assert.ok(has('Tracks')&&has('Routes'),'keypad Enter opens Menu exactly once');
key(27);tick();assert.equal(runtime._native_page_value(),14);
selectMenu();press();assert.ok(has('Tracks')&&has('Routes'),'encoder click opens Menu exactly once');
key(27);tick();
// A focused text editor keeps literal h, w, s, and v. Encoder navigation can
// leave it without first finding an undisclosed edit-mode shortcut.
runtime._native_open_page(7);tick();
const editor=snapshot().find(item=>item.input);assert.ok(editor);
runtime._native_focus(editor.id);
runtime.ccall('native_set_text',null,['number','string'],[editor.id,'']);
for(const k of ['h','w','s','v'])key(k);
assert.equal(snapshot().find(item=>item.input)?.text,'hwsv');
wheel(1);assert.notEqual(runtime._native_input_focus(),editor.id);
assert.equal(runtime._native_ime_proxy_protected(),1,'active IME proxy keeps virtual focus and literal h');
// Destroy pages with Help still open: no stale restore group or orphan modal.
for(let cycle=0;cycle<20;cycle++){
  runtime._native_open_page(0);tick();key('h');
  runtime._native_open_page(13);tick();wheel(1);key('h');key('h');
  assert.ok(runtime._native_input_focus());
}
runtime._native_open_page(10);tick();
assert.equal(runtime._native_widget_leases(),0);
console.log('Pager: physical WASD/V, all Music/Controls buttons and sliders, encoder press ownership, 19 app focus/help routes and modal teardown passed.');
