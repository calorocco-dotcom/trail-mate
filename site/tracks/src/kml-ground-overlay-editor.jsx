import React,{useEffect,useId,useMemo,useRef,useState} from 'react';
import {Button,Card} from 'animal-island-ui';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {kmlOverlayEditorText} from '../../i18n/kml-overlay-editor-labels.js';
import {groundOverlayEditors,editGroundOverlay,addGroundOverlay,removeGroundOverlay} from './kml-ground-overlay-edit.js';
import {readTrackDraft} from './draft-store.js';
import {overlayNumberInput} from './kml-overlay-input.js';
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const wrap=longitude=>((longitude+180)%360+360)%360-180;
function blank(bounds){
 let box={north:'',south:'',east:'',west:'',rotation:'0'};
 if(bounds){let {north,south,east,west}=bounds;if(north===south){north=Math.min(90,north+.005);south=Math.max(-90,south-.005);}if(east===west){east=wrap(east+.005);west=wrap(west-.005);}box={north:String(north),south:String(south),east:String(east),west:String(west),rotation:'0'};}
 return {name:'',description:'',visible:true,href:'',mode:'box',box,points:Array.from({length:4},()=>({longitude:'',latitude:'',altitude:null})),color:'ffffffff',drawOrder:'0',cropEnabled:false,crop:{x:'0',y:'0',w:'-1',h:'-1'}};
}
function values(record){return Object.fromEntries(['name','description','visible','href','mode','box','points','color','drawOrder','cropEnabled','crop'].map(key=>[key,record[key]]));}
function changes(base,form,creating,locale,inputLocales){
 const numberInput=(value,path)=>overlayNumberInput(value,inputLocales[path]??locale);
 const patch={};for(const key of ['name','description','visible','href','color','drawOrder'])if(creating||form[key]!==base[key])patch[key]=form[key];
 if('drawOrder' in patch)patch.drawOrder=numberInput(patch.drawOrder,'drawOrder');
 if(creating||form.mode!==base.mode||!same(form[form.mode==='box'?'box':'points'],base[form.mode==='box'?'box':'points']))patch.footprint=form.mode==='box'?{mode:'box',box:Object.fromEntries(Object.entries(form.box).map(([key,value])=>[key,numberInput(value,'box.'+key)]))}:{mode:'quad',points:form.points.map((point,index)=>({...point,longitude:numberInput(point.longitude,'points.'+index+'.longitude'),latitude:numberInput(point.latitude,'points.'+index+'.latitude')}))};
 if(form.cropEnabled&&(creating||!base.cropEnabled||!same(form.crop,base.crop)))patch.crop=Object.fromEntries(Object.entries(form.crop).map(([key,value])=>[key,numberInput(value,'crop.'+key)]));else if(!form.cropEnabled&&base.cropEnabled)patch.crop=null;
 return patch;
}
export function KmlGroundOverlayEditor({model,draft,bounds,disabled=false,onApply,onDirty}){
 const locale=useManagementLocale(),text=kmlOverlayEditorText(locale),number=new Intl.NumberFormat(locale,{maximumFractionDigits:1}),listId=useId();
 const records=useMemo(()=>groundOverlayEditors(model),[model]),[selected,setSelected]=useState(()=>records.length?'0':''),[form,setForm]=useState(null),[base,setBase]=useState(null),[notice,setNotice]=useState(null),[references,setReferences]=useState([]);
 const dirtyCallback=useRef(onDirty),inputLocales=useRef({});dirtyCallback.current=onDirty;
 const creating=selected==='new',record=records[Number(selected)],pending=Boolean(form&&base&&(creating||!same(base,form)));
 useEffect(()=>{setSelected(index=>index==='new'?index:records.length?String(Math.min(Number(index)||0,records.length-1)):'');},[model]);
 useEffect(()=>{const current=selected==='new'?blank(bounds):selected!==''&&records[Number(selected)]?values(records[Number(selected)]):null;inputLocales.current={};setBase(current);setForm(current);setNotice(null);},[model,selected]);
 useEffect(()=>{dirtyCallback.current?.(pending);return()=>dirtyCallback.current?.(false);},[pending]);
 useEffect(()=>{let active=true;setReferences([]);readTrackDraft(draft.id).then(snapshot=>{if(active&&snapshot.version===draft.version)setReferences(snapshot.attachmentRows.filter(row=>['image/png','image/jpeg'].includes(row.mime)).map(row=>row.reference));}).catch(()=>{});return()=>{active=false;};},[draft.id,draft.version]);
 function edit(key,value){if(key==='drawOrder')inputLocales.current[key]=locale;setForm(current=>({...current,[key]:value}));setNotice(null);}
 function nested(group,key,value){inputLocales.current[group+'.'+key]=locale;setForm(current=>({...current,[group]:{...current[group],[key]:value}}));setNotice(null);}
 function corner(index,key,value){inputLocales.current['points.'+index+'.'+key]=locale;setForm(current=>({...current,points:current.points.map((point,i)=>i===index?{...point,[key]:value}:point)}));setNotice(null);}
 function positionMode(mode){if(mode==='quad'&&form.points.every(p=>p.longitude===''&&p.latitude==='')){const {north,south,east,west}=form.box;const points=[[west,south],[east,south],[east,north],[west,north]].map(([longitude,latitude],index)=>{inputLocales.current['points.'+index+'.longitude']=inputLocales.current['box.'+(index===0||index===3?'west':'east')]??locale;inputLocales.current['points.'+index+'.latitude']=inputLocales.current['box.'+(index<2?'south':'north')]??locale;return {longitude,latitude,altitude:null};});setForm({...form,mode,points});}else setForm({...form,mode});setNotice(null);}
 function failure(error){setNotice(error?.code==='KML_OVERLAY_AMBIGUOUS'?'ambiguous':error?.code==='KML_OVERLAY_REFERENCED'?'referenced':'invalid');}
 function apply(){if(disabled||!pending)return;try{const patch=changes(base,form,creating,locale,inputLocales.current),result=onApply(source=>creating?addGroundOverlay(source,patch):editGroundOverlay(source,Number(selected),patch));if(!result?.ok){failure(result?.error);return;}dirtyCallback.current?.(false);if(creating)setSelected(String(records.length));}catch(error){failure(error);}}
 function discard(){if(creating){setSelected(records.length?'0':'');setForm(null);setBase(null);}else setForm(base);setNotice(null);dirtyCallback.current?.(false);}
 function remove(){if(disabled||creating||!record||!confirm(text.confirmRemove))return;const result=onApply(source=>removeGroundOverlay(source,record.index));if(!result?.ok)failure(result?.error);}
 const color=form&&/^[\da-f]{8}$/i.test(form.color)?form.color.toLowerCase():'ffffffff',rgb='#'+color.slice(6,8)+color.slice(4,6)+color.slice(2,4),alpha=parseInt(color.slice(0,2),16);
 const numeric=(key,label,group)=> <label>{label}<input dir="ltr" inputMode="decimal" aria-label={label} value={group?form[group][key]:form[key]} onChange={event=>group?nested(group,key,event.target.value):edit(key,event.target.value)}/></label>;
 return <Card className="tracks-overlay-editor"><h2>{text.title}</h2><p>{text.intro}</p>{disabled&&<p className="tracks-help">{text.locked}</p>}
  <fieldset disabled={disabled} className="tracks-edit-lock">
   <div className="tracks-actions"><Button disabled={pending} onClick={()=>setSelected('new')}>{text.new}</Button></div>
   {records.length>0&&<label>{text.select}<select aria-label={text.select} value={selected} disabled={pending} onChange={event=>setSelected(event.target.value)}>{records.map(row=><option key={row.index} value={row.index}>{[...row.path,row.name||text.title].join(' / ')} · {number.format(row.index+1)}</option>)}{creating&&<option value="new">{text.new}</option>}</select></label>}
   {!records.length&&!creating&&<p>{text.empty}</p>}
   {form&&<><div className="tracks-fields"><label>{text.name}<input aria-label={text.name} value={form.name} onChange={event=>edit('name',event.target.value)}/></label><label className="tracks-overlay-toggle"><input type="checkbox" aria-label={text.visible} checked={form.visible} onChange={event=>edit('visible',event.target.checked)}/>{text.visible}</label></div>
    {!creating&&record?.inheritedHidden&&<p className="tracks-help">{text.inherited}</p>}
    <label>{text.description}<textarea aria-label={text.description} rows={3} value={form.description} onChange={event=>edit('description',event.target.value)}/></label>
    <label>{text.href}<input dir="ltr" list={listId} aria-label={text.href} value={form.href} onChange={event=>edit('href',event.target.value)}/></label><datalist id={listId}>{references.map(reference=><option key={reference} value={reference}/>)}</datalist><p className="tracks-help">{text.hrefHelp}</p>
    <label>{text.mode}<select aria-label={text.mode} value={form.mode} onChange={event=>positionMode(event.target.value)}><option value="box">{text.box}</option><option value="quad">{text.quad}</option></select></label>
    {form.mode==='box'?<><div className="tracks-fields">{['north','south','east','west','rotation'].map(key=><React.Fragment key={key}>{numeric(key,text[key],'box')}</React.Fragment>)}</div><p className="tracks-help">{text.boxHelp}</p></>:<><div className="tracks-overlay-corners">{['ll','lr','ur','ul'].map((key,index)=><fieldset key={key}><legend>{text[key]}</legend><div className="tracks-fields">{['longitude','latitude'].map(axis=><label key={axis}>{text[axis]}<input dir="ltr" inputMode="decimal" aria-label={text[key]+' · '+text[axis]} value={form.points[index][axis]} onChange={event=>corner(index,axis,event.target.value)}/></label>)}</div></fieldset>)}</div><p className="tracks-help">{text.quadHelp}</p></>}
    <div className="tracks-fields"><label>{text.color}<input type="color" aria-label={text.color} value={rgb} onChange={event=>{const value=event.target.value.slice(1);edit('color',color.slice(0,2)+value.slice(4,6)+value.slice(2,4)+value.slice(0,2));}}/></label><label>{text.opacity} · <bdi>{number.format(alpha/255*100)}%</bdi><input type="range" aria-label={text.opacity} aria-valuetext={number.format(alpha/255*100)+'%'} min="0" max="255" step="1" value={alpha} onChange={event=>edit('color',Number(event.target.value).toString(16).padStart(2,'0')+color.slice(2))}/></label>{numeric('drawOrder',text.order)}</div><p className="tracks-help">{text.orderHelp}</p>
    <label className="tracks-overlay-toggle"><input type="checkbox" aria-label={text.crop} checked={form.cropEnabled} onChange={event=>edit('cropEnabled',event.target.checked)}/>{text.crop}</label>
    {form.cropEnabled&&<><div className="tracks-fields">{numeric('x',text.x,'crop')}{numeric('y',text.y,'crop')}{['w','h'].map(key=><div key={key}><label>{text[key]}<select aria-label={text[key]} value={form.crop[key]==='-1'?'full':'custom'} onChange={event=>nested('crop',key,event.target.value==='full'?'-1':'1')}><option value="full">{text.full}</option><option value="custom">{text.custom}</option></select></label>{form.crop[key]!=='-1'&&numeric(key,text[key]+' (px)','crop')}</div>)}</div><p className="tracks-help">{text.cropHelp}</p></>}
    {notice&&<p role="alert">{text[notice]}</p>}{pending&&<p role="status" className="tracks-help">{text.pending}</p>}
    <div className="tracks-actions"><Button type="primary" disabled={!pending} onClick={apply}>{text.apply}</Button><Button disabled={!pending} onClick={discard}>{text.discard}</Button>{!creating&&<Button disabled={pending} onClick={remove}>{text.remove}</Button>}</div>
   </>}
  </fieldset>
 </Card>;
}
