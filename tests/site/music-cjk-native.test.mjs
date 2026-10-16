import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {framePng} from './native-frame-png.mjs';

const fixture = new URL('./fixtures/music-cjk/', import.meta.url);
const font = await readFile(new URL('font.bin', fixture));
const charset = (await readFile(new URL('charset.txt', fixture), 'utf8')).trim();
const ranges = [...new Set(charset)].map(c => `0x${c.codePointAt(0).toString(16)}`).join(',');
const boards = [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240],
  ['t-display-p4-tft',1168,540],['t-display-p4-amoled',1232,568]];
for (const [device,width,height] of boards.filter(([name]) => !process.env.MUSIC_UI_DEVICES || process.env.MUSIC_UI_DEVICES.split(',').includes(name))) {
  const base = new URL(`../../site/assets/native/${device}`, import.meta.url);
  const {default:create} = await import(`${base}.js`);
  const data = await readFile(new URL(`${base}.data`));
  let runtime, frame;
  runtime = await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);},
    onFrame(ptr,w,h){assert.equal(w,width);assert.equal(h,height);frame=Buffer.from(runtime.HEAPU8.subarray(ptr,ptr+w*h*2));},
    print(message){if(process.env.MUSIC_UI_DEBUG)console.log(message);},printErr:console.error});
  // Only the test filesystem receives a CJK pack. The UI remains English.
  const root = '/trailmate/packs/fonts/music-test-cjk';
  runtime.FS.mkdirTree(root);
  runtime.FS.writeFile(`${root}/font.bin`, font);
  runtime.FS.writeFile(`${root}/ranges.txt`, ranges);
  runtime.FS.writeFile(`${root}/manifest.ini`, 'kind=font\nid=music-test-cjk\ndisplay_name=Music test CJK\nusage=content\nsource=binfont\nfile=font.bin\nranges=ranges.txt\nestimated_ram_bytes=8192\n');
  // Advance past both the page's 500 ms and dashboard's 1 s refresh cadence.
  const tick = () => {for(let i=0;i<26;i++)runtime._native_tick(50);};
  const snapshot = () => JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const click = text => {
    const item = snapshot().find(item => item.button && item.text === text);
    assert.ok(item, `${device}: ${text}`);
    runtime._native_activate(item.id);tick();
  };
  const text = (title='空白格') => runtime.ccall('native_preview_music_text', null,
    ['string','string','string','string','string','string'],
    [title,'蔡健雅','中文专辑','上一行歌词','中文歌词测试','下一行歌词']);
  const verify = async (name, expected) => {
    tick();
    if(process.env.MUSIC_UI_ARTIFACT_DIR) {
      const directory = join(process.env.MUSIC_UI_ARTIFACT_DIR, device);
      await mkdir(directory,{recursive:true});
      await writeFile(join(directory,`cjk-${name}.png`),framePng(frame,width,height));
      await writeFile(join(directory,`cjk-${name}.json`),JSON.stringify(snapshot(),null,2));
    }
    for(const value of expected) assert.ok(snapshot().some(item => item.text.includes(value)), `${device}:${name}: ${value}; widget page=${runtime._native_widget_page()}`);
    assert.equal(runtime._native_music_content_font_errors(), 0, `${device}:${name}: content scope and real CJK glyph coverage`);
  };
  runtime._native_init(device==='pager'?0:1);
  runtime._native_preview_music_playing(1);text();runtime._native_open_page(19);tick();
  await verify('playing',['Now Playing','空白格','蔡健雅','中文专辑']);
  assert.equal(runtime._native_music_geometry(),0,`${device}: CJK player geometry`);
  click('Lyrics');await verify('lyrics',['中文歌词测试','上一行歌词','下一行歌词']);
  click('\uf053');click('Menu');click('Track info');text();
  await verify('metadata',['Track info','空白格','蔡健雅','中文专辑']);
  click('\uf053');click('Library');text();await verify('library',['空白格','蔡健雅','中文专辑']);
  click('Browse');click('New playlist');
  const editor = snapshot().find(item => item.input);assert.ok(editor);
  runtime.ccall('native_set_text',null,['number','string'],[editor.id,'晚间歌单']);
  await verify('playlist-editor',['晚间歌单']);
  click('Create');await verify('playlists',['晚间歌单']);
  click('晚间歌单');await verify('category',['晚间歌单']);
  click('Browse');click('Delete playlist');await verify('delete-playlist',['晚间歌单']);
  click('Cancel');
  runtime._native_preview_music_playing(1);runtime._native_open_page(10);tick();
  if(device.startsWith('t-display')) {click('\uf054');click('\uf054');}
  else {
    const map=snapshot().find(item => item.button && item.text==='Map');assert.ok(map);
    runtime._native_focus(map.id);runtime._native_key((device==='pager'?'v':'a').charCodeAt(0));tick();
    for(let i=0;i<4;i++){runtime._native_encoder(1);tick();}
  }
  text();await verify('widget',['Music','空白格','蔡健雅','中文专辑']);
  text('中文测试');await verify('widget-update',['中文测试']);
  runtime._native_open_page(11);tick();
  assert.equal(runtime._native_widget_leases(),0,`${device}: page/widget PSRAM state released`);
  console.log(`${device}: CJK library, metadata, lyrics, playlists and widget resolve real content glyphs in English UI.`);
}
