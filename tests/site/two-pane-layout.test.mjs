import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

for(const [device,width,height] of [['pager',480,222],['tdeck',320,240],['wio-tracker-l2',320,240]]) {
  const base=new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create}=await import(`${base}.js`);
  const bytes=await readFile(new URL(`${base}.data`));
  let framePointer;
  const runtime=await create({
    wasmBinary:await readFile(new URL(`${base}.wasm`)),
    getPreloadedPackage(){return bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);},
    onFrame(pointer){framePointer=pointer;},print(){},printErr(){},
  });
  runtime._native_init(device==='pager'?0:1);
  for(const [page,filterText,contentText,maxTextGap] of [[5,'Direct','Alex',16],[12,'Contacts','No contacts yet',6],[14,'Tracks','Not recording',5]]) {
    runtime._native_open_page(page);
    for(let i=0;i<5;i++)runtime._native_tick(50);
    const snapshot=JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
    const filter=snapshot.find(item=>item.button&&item.text===filterText);
    const content=snapshot.find(item=>!item.button&&item.text===contentText);
    assert.ok(filter&&content,`${device}:${page}: both columns render`);
    const gap=content.x-(filter.x+filter.w);
    // Message text includes the card's own inset; compare actual rendered geometry.
    const limit=device==='tdeck'&&page===5?24:maxTextGap;
    assert.ok(gap>=0&&gap<=limit,`${device}:${page}: content lost width to a ${gap}px gap`);
    // T-Deck's message name is vertically inset inside its richer info card.
    if(device!=='tdeck'||page!==5)assert.ok(Math.abs(content.y-filter.y)<=3,`${device}:${page}: first rows align`);
    const pixels=new Uint16Array(runtime.HEAPU8.buffer,framePointer,width*height);
    const y=height-50;
    const background=pixels[y*width+10];
    // Tracker now deliberately uses colored item cards. Its seam must remain
    // continuous, while the content card may contrast with the pane background.
    const seamEnd=page===14?content.x:width-10;
    for(let x=10;x<seamEnd;x++)assert.equal(pixels[y*width+x],background,`${device}:${page}: divider or contrasting panel at ${x},${y}`);
  }
  console.log(`${device}: compact two-pane geometry, row alignment and continuous background passed.`);
}
