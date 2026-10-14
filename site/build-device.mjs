import {build} from 'esbuild';
await build({entryPoints:['device/src/device.jsx'],bundle:true,outdir:'device/dist',entryNames:'device',format:'esm',target:'es2022',minify:true,legalComments:'linked',loader:{'.png':'file','.webp':'file','.woff2':'file','.woff':'file','.ttf':'file','.svg':'file'},sourcemap:true});
console.log('Device management page built.');
