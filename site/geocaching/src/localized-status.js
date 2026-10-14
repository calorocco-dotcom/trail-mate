// Stores presentation descriptors only. It never owns a worker or a request.
export function createLocalizedStatus({getText,getNode,formatValue=String}){
 const bindings=new Map();
 const render=({id,key,values,suffix})=>{
  const template=getText()[key];
  if(typeof template!=='string')throw Error(`Missing Geocaching status translation: ${key}`);
  const node=getNode(id);if(!node)throw Error(`Missing Geocaching status target: ${id}`);
  node.textContent=template.replace(/\{(\w+)\}/g,(token,name)=>Object.hasOwn(values,name)?formatValue(values[name]):token)+(suffix?'\n'+suffix:'');
 };
 return {
  set(id,key,values={},suffix=''){const binding={id,key,values:{...values},suffix};render(binding);bindings.set(id,binding);},
  raw(id,message){const node=getNode(id);if(!node)throw Error(`Missing Geocaching status target: ${id}`);node.textContent=message;bindings.delete(id);},
  get(id){const binding=bindings.get(id);return binding?{...binding,values:{...binding.values}}:null;},
  refresh(){for(const binding of bindings.values())render(binding);},
 };
}
