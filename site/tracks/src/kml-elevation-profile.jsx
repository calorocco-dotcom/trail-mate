import React,{useEffect,useMemo,useState} from 'react';
import {buildKmlElevationProfile} from './kml-elevation.js';
import {useKmlWorkspaceText} from '../../i18n/management-copy.jsx';
import {configElevationText} from '../../i18n/kml-elevation-labels.js';
export function KmlElevationProfile({model,onPoint}){
 const {locale}=useKmlWorkspaceText(),text=configElevationText(locale),profile=useMemo(()=>buildKmlElevationProfile(model),[model]),[selected,setSelected]=useState(null);
 useEffect(()=>{const first=profile.samples[0]??null;setSelected(first?0:null);onPoint?.(first);},[profile,onPoint]);
 const number=new Intl.NumberFormat(locale,{maximumFractionDigits:1}),sample=selected===null?null:profile.samples[selected];
 const x=value=>40+value/Math.max(profile.distanceM,1)*540,y=value=>190-(value-profile.minAltitude)/Math.max(profile.maxAltitude-profile.minAltitude,1)*160;
 const path=profile.samples.map((point,index)=>`${index&&profile.samples[index-1].group===point.group?'L':'M'}${x(point.distanceM)},${y(point.altitude)}l0,0`).join(' ');
 const choose=index=>{setSelected(index);onPoint?.(profile.samples[index]);};
 const current=sample?`${text.distance}: ${number.format(sample.distanceM/1000)} km · ${text.altitude}: ${number.format(sample.altitude)} m`:text.pick;
 return <section className="tracks-elevation" aria-label={text.title}>
  <h3>{text.title}</h3>{!profile.eligible?<p>{text.empty}</p>:<>
   <p className="tracks-help">{text.help}</p><div className="tracks-stats"><span>{text.min}: {number.format(profile.minAltitude)} m</span><span>{text.max}: {number.format(profile.maxAltitude)} m</span></div>
   <svg viewBox="0 0 600 230" role="img" aria-label={text.title} onClick={event=>{const bounds=event.currentTarget.getBoundingClientRect(),scale=Math.min(bounds.width/600,bounds.height/230),inset=(bounds.width-600*scale)/2,distance=Math.max(0,Math.min(1,((event.clientX-bounds.left-inset)/scale-40)/540))*profile.distanceM;let index=0;for(let i=1;i<profile.samples.length;i++)if(Math.abs(profile.samples[i].distanceM-distance)<Math.abs(profile.samples[index].distanceM-distance))index=i;choose(index);}}>
    <path d="M40,20 V190 H580" fill="none" stroke="currentColor" opacity=".4"/><path className="tracks-elevation-line" d={path} fill="none" stroke="#478e79" strokeWidth="2" strokeLinecap="round"/>
    <text x="40" y="215">0 km</text><text x="580" y="215" textAnchor="end">{number.format(profile.distanceM/1000)} km</text>
    {sample&&<><path d={`M${x(sample.distanceM)},20 V190`} stroke="#a57444" strokeDasharray="4 4"/><circle cx={x(sample.distanceM)} cy={y(sample.altitude)} r="5" fill="#a57444"/></>}
   </svg>
   <label>{text.pick}<input type="range" min="0" max={profile.samples.length-1} value={selected??0} aria-valuetext={current} onChange={event=>choose(Number(event.target.value))}/></label>
   <p className="tracks-elevation-selected" aria-live="polite">{current}</p>{profile.sampled&&<p className="tracks-help">{text.sampled}</p>}
  </>}
 </section>;
}
