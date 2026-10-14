import {build} from 'esbuild';
await build({entryPoints:['tracks/src/page.jsx'],bundle:true,outdir:'tracks/dist',entryNames:'tracks',format:'esm',target:'es2022',minify:true,legalComments:'linked',loader:{'.png':'file','.webp':'file','.svg':'file','.woff2':'file','.woff':'file','.ttf':'file'},sourcemap:true});
console.log('Track workspace built.');
