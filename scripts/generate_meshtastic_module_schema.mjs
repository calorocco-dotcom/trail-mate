import {readFile,writeFile} from 'node:fs/promises';
const files=['localonly.pb.h','module_config.pb.h'];
const root='meshtastic_LocalModuleConfig';
const source=(await Promise.all(files.map(file=>readFile(new URL('../modules/core_chat/generated/meshtastic/'+file,import.meta.url),'utf8')))).join('\n').replace(/\/\*[\s\S]*?\*\//g,'');
const enums={};
for(const matched of source.matchAll(/typedef enum _(\w+)\s*\{([\s\S]*?)\}\s*\w+;/g)){
 const name=matched[1],values=[];
 for(const row of matched[2].matchAll(/(\w+)\s*=\s*(-?(?:0x[0-9a-fA-F]+|\d+))/g))values.push({name:row[1].replace(name+'_',''),value:Number(row[2])});
 enums[name]=values;
}
const messages={};
function visit(name){
 if(messages[name])return;
 const list=source.match(new RegExp('#define '+name+'_FIELDLIST\\(X, a\\)([\\s\\S]*?)(?=\\n#define)'));
 const struct=source.match(new RegExp('typedef struct _'+name+'\\s*\\{([\\s\\S]*?)\\}\\s*'+name+';'));
 const size=source.match(new RegExp('#define '+name+'_size\\s+(\\d+)'));
 if(!list||!struct||!size)throw Error('Missing nanopb definition: '+name);
 const message={maxBytes:Number(size[1]),fields:[]};messages[name]=message;
 for(const row of list[1].matchAll(/X\(a,\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\d+)\)/g)){
  const [,allocation,cardinality,type,field,number]=row;
  if(allocation!=='STATIC'||!['SINGULAR','OPTIONAL','REPEATED'].includes(cardinality)||!['BOOL','UINT32','INT32','STRING','MESSAGE','UENUM'].includes(type))throw Error('Unsupported field descriptor: '+name+'.'+field);
  const declaration=struct[1].match(new RegExp('(?:^|\\n)\\s*(\\w+)\\s+'+field+'(?:\\[(\\d+)\\])?\\s*;'));
  if(!declaration)throw Error('Missing storage definition: '+name+'.'+field);
  const item={name:field,number:Number(number),type,cardinality};
  if(type==='STRING'){if(declaration[1]!=='char'||!declaration[2])throw Error('Unbounded string: '+field);item.maxBytes=Number(declaration[2])-1;}
  if(type==='UINT32')item.max=declaration[1]==='uint8_t'?255:declaration[1]==='uint16_t'?65535:4294967295;
  if(type==='INT32'){item.min=-2147483648;item.max=2147483647;}
  if(type==='UENUM'){if(!enums[declaration[1]]?.length)throw Error('Missing enum: '+field);item.options=enums[declaration[1]];}
  if(type==='MESSAGE'){
   const nested=source.match(new RegExp('#define '+name+'_'+field+'_MSGTYPE\\s+(\\w+)'));
   if(!nested)throw Error('Missing nested message: '+field);item.message=nested[1];
  }
  if(cardinality==='REPEATED'){if(!declaration[2])throw Error('Unbounded repeated field: '+field);item.maxCount=Number(declaration[2]);}
  if(name===root&&field==='version')item.readOnly=true;
  message.fields.push(item);
 }
 if(!message.fields.length)throw Error('No fields extracted: '+name);
 for(const field of message.fields)if(field.message)visit(field.message);
}
visit(root);
const result={root,messages};
const output='// Generated from checked-in nanopb headers; run scripts/generate_meshtastic_module_schema.mjs.\nexport const MODULE_SCHEMA='+JSON.stringify(result,null,2)+';\n';
const destination=new URL('../site/device/src/meshtastic-module-schema.js',import.meta.url);
if(process.argv.includes('--check')){if(await readFile(destination,'utf8')!==output)throw Error('Module schema is out of date; regenerate it.');}
else await writeFile(destination,output);
console.log(`Module schema: ${Object.keys(messages).length} messages, ${Object.values(messages).reduce((sum,message)=>sum+message.fields.length,0)} fields; ${process.argv.includes('--check')?'checked':'generated'}.`);
