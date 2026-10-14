import React,{useEffect,useState} from 'react';
import {Button} from 'animal-island-ui';
import {useKmlWorkspaceText} from '../../i18n/management-copy.jsx';
import {kmlContentText} from '../../i18n/kml-content-labels.js';
import {kmlResourceText} from '../../i18n/kml-resource-labels.js';
import {kmlScreenText} from '../../i18n/kml-screen-labels.js';
function PagedContents({items,text,number,render}){
 const [page,setPage]=useState(0);useEffect(()=>setPage(0),[items]);
 const pages=Math.max(1,Math.ceil(items.length/25)),current=Math.min(page,pages-1);
 return <>
  {items.slice(current*25,(current+1)*25).map(render)}
  {pages>1&&<div className="tracks-actions">
   <Button disabled={current===0} onClick={()=>setPage(current-1)}>{text.previous}</Button>
   <span>{text.page.replace('{page}',number.format(current+1)).replace('{pages}',number.format(pages))}</span>
   <Button disabled={current===pages-1} onClick={()=>setPage(current+1)}>{text.next}</Button>
  </div>}
 </>;
}
export function KmlContentDetails({model}){
 const {locale,text}=useKmlWorkspaceText(),copy=kmlContentText(locale),resourceText=kmlResourceText(locale),number=new Intl.NumberFormat(locale);
 return <details className="shared-kml-contents">
  <summary>{copy.title} · {number.format(model.features.length)}</summary>
  {!model.features.length&&<p>{copy.empty}</p>}
  <PagedContents items={model.features} text={copy} number={number} render={(feature,index)=><section className="shared-kml-feature" key={index}>
   <h4><bdi>{feature.name||text.unnamedFeature}</bdi></h4>
   {feature.path.filter(Boolean).length>0&&<p><bdi>{feature.path.filter(Boolean).join(' / ')}</bdi></p>}
   {feature.id&&<p>ID: <bdi>{feature.id}</bdi></p>}
   {feature.description&&<p style={{whiteSpace:'pre-wrap',overflowWrap:'anywhere'}}>{feature.description}</p>}
   {feature.timestamp&&<p>{text.timestamp.replace('{value}',feature.timestamp)}</p>}
   {feature.timespan&&<p><bdi>{feature.timespan.begin||text.unknown} — {feature.timespan.end||text.unknown}</bdi></p>}
   {feature.styleUrl&&<p>{text.style.replace('{value}',feature.styleUrl)}</p>}
   {feature.extendedData.length>0&&<details className="shared-kml-extensions">
    <summary>{text.extensions.replace('{count}',number.format(feature.extendedData.length))}</summary>
    <PagedContents items={feature.extendedData} text={copy} number={number} render={(data,index)=><dl key={index}>
     <dt><bdi>{data.displayName||data.name||text.unknown}</bdi></dt>
     <dd style={{whiteSpace:'pre-wrap',overflowWrap:'anywhere'}}>{data.value}</dd>
    </dl>}/>
   </details>}
  </section>}/>
  {model.resources?.length>0&&<details className="shared-kml-resources">
   <summary>{resourceText.title} · {number.format(model.resources.length)}</summary><p>{resourceText.notice}</p>
   <PagedContents items={model.resources} text={copy} number={number} render={(resource,index)=><section className="shared-kml-resource" key={index} style={{minWidth:0,overflowWrap:'anywhere'}}>
    <h4><bdi>{resource.name||text.unnamedFeature}</bdi> · <bdi>{resource.type}</bdi></h4>
    {resource.type==='ScreenOverlay'&&<p>{kmlScreenText(locale).intro}</p>}
    {resource.id&&<p>ID: <bdi>{resource.id}</bdi></p>}
    {resource.path.length>0&&<p><bdi>{resource.path.join(' / ')}</bdi></p>}
    {resource.href&&<p>{resourceText.reference}: <code dir="ltr" style={{overflowWrap:'anywhere'}}>{resource.href}</code></p>}
   </section>}/>
  </details>}
 </details>;
}
