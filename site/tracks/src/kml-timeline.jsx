import React,{useEffect,useMemo,useState} from 'react';
import {Button} from 'animal-island-ui';
import {buildKmlTimeline,pointAtKmlTime} from './kml-timeline.js';
import {useKmlWorkspaceText} from '../../i18n/management-copy.jsx';
import {kmlTimelineText} from '../../i18n/kml-timeline-labels.js';
export function KmlTimeline({model,onPoint}){
 const {locale,text:workspace}=useKmlWorkspaceText(),text=kmlTimelineText(locale),timeline=useMemo(()=>buildKmlTimeline(model),[model]);
 const [selected,setSelected]=useState(0),[offset,setOffset]=useState(0),[speed,setSpeed]=useState(1),[playing,setPlaying]=useState(false),[engaged,setEngaged]=useState(false);
 const track=timeline.tracks[Math.min(selected,timeline.tracks.length-1)],active=useMemo(()=>track?pointAtKmlTime(track,track.start+offset*1000):null,[track,offset]);
 useEffect(()=>{setSelected(0);setOffset(0);setPlaying(false);setEngaged(false);},[timeline]);
 useEffect(()=>{if(engaged)onPoint?.(active);},[track,active?.index,engaged,onPoint]);
 useEffect(()=>{if(!playing||!track)return;const start=performance.now(),from=offset;const timer=setInterval(()=>{const next=Math.min(track.durationS,from+(performance.now()-start)*speed/1000);setOffset(next);if(next>=track.durationS)setPlaying(false);},100);return()=>clearInterval(timer);},[playing,track,speed]);
 useEffect(()=>{const pause=()=>{if(document.hidden)setPlaying(false);};document.addEventListener('visibilitychange',pause);return()=>document.removeEventListener('visibilitychange',pause);},[]);
 const number=new Intl.NumberFormat(locale,{maximumFractionDigits:1}),date=new Intl.DateTimeFormat(locale,{timeZone:'UTC',dateStyle:'medium',timeStyle:'medium'}),stamp=time=>date.format(time)+' UTC';
 return <section className="tracks-timeline" aria-label={text.title}><h3>{text.title}</h3>
  {!track?<p>{text.empty}</p>:<>
   <p className="tracks-help">{text.help}</p>
   <label>{text.segment}<select aria-label={text.segment} value={Math.min(selected,timeline.tracks.length-1)} onChange={event=>{setPlaying(false);setSelected(Number(event.target.value));setOffset(0);setEngaged(true);}}>{timeline.tracks.map((row,index)=><option key={index} value={index}>{model.features[row.feature].name||workspace.unnamedFeature} · {number.format(index+1)}</option>)}</select></label>
   <p><bdi>{stamp(track.start)} — {stamp(track.end)}</bdi></p><p>{text.duration}: {number.format(track.durationS)} s</p>
   <div className="tracks-actions"><Button disabled={!track.durationS} onClick={()=>{
    if(playing)setPlaying(false);
    else{if(offset>=track.durationS)setOffset(0);setEngaged(true);setPlaying(true);}
   }}>{playing?text.pause:text.play}</Button><Button onClick={()=>{setPlaying(false);setOffset(0);setEngaged(true);}}>{text.reset}</Button><label>{text.speed}<select aria-label={text.speed} value={speed} onChange={event=>setSpeed(Number(event.target.value))}>{[1,5,20,100].map(value=><option key={value} value={value}>{number.format(value)}×</option>)}</select></label></div>
   <label>{text.position}<input type="range" min="0" max={track.durationS} step="0.001" value={Math.min(offset,track.durationS)} aria-valuetext={active?stamp(active.time):text.empty} onChange={event=>{setPlaying(false);setOffset(Number(event.target.value));setEngaged(true);}}/></label>
   <p>{text.elapsed}: {number.format(offset)} / {number.format(track.durationS)} s</p><p aria-live="polite">{text.recorded}: <bdi>{stamp(active.time)}</bdi></p>
  </>}{timeline.rejected>0&&<p className="tracks-help">{text.invalid} ({number.format(timeline.rejected)})</p>}
 </section>;
}
