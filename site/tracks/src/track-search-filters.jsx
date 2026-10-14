import React from 'react';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackMetadataText} from '../../i18n/track-metadata-labels.js';
import {trackSearchText} from '../../i18n/track-search-labels.js';
export function TrackSearchFilters({filter,onChange,supported=false,disabled=false}){
 const locale=useManagementLocale(),text=trackMetadataText(locale),search=trackSearchText(locale);
 return <div className="track-search-filters">
  <label>{text.activity}<select aria-label={text.activity} value={filter.activity??''} disabled={disabled||!supported} onChange={event=>onChange({...filter,activity:event.target.value===''?null:Number(event.target.value)})}>
   <option value="">{search.all}</option>{text.activities.map((label,index)=><option key={index} value={index}>{label}</option>)}
   {filter.activity>8&&<option value={filter.activity}>{text.unknown.replace('{value}',new Intl.NumberFormat(locale).format(filter.activity))}</option>}
  </select></label>
  <label>{text.tags}<textarea aria-label={text.tags} rows={2} value={(filter.tags??[]).join('\n')} disabled={disabled||!supported} onChange={event=>onChange({...filter,tags:event.target.value.split('\n')})}/></label>
  <small>{supported?search.help:search.unsupported}</small>
 </div>;
}
