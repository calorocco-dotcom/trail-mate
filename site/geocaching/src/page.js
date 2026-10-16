import L from 'leaflet';
import cacheIconUrl from '../../assets/geocaching.svg';
import {mountIslandShell} from './island-shell.jsx';
import {countryBounds, countryMatches, regionDisplayGeometry} from './country-boundaries.js';
import {requestNearbyPosition} from './nearby-location.js';
import {createLiveRefresh} from './live-refresh.js';
import './page.css';
import './theme.css';
import {initializeManagementPage,getManagementLanguageRuntime} from '../../i18n/management-language.jsx';
import {geocachingBrowseText} from '../../i18n/geocaching-browse-labels.js';
import {geocachingStatusText} from '../../i18n/geocaching-status-labels.js';
import {createLocalizedStatus} from './localized-status.js';
import {registerGeocachingNetwork} from './web-network.js';

const initialLanguage=await initializeManagementPage();
mountIslandShell();
let locale=initialLanguage.locale,chinese=locale.startsWith('zh');
const tr = (en, zh) => chinese ? zh : en;
let labels=geocachingBrowseText(locale);
let statuses=geocachingStatusText(locale);
const statusText=key=>statuses[key];
const label=(key,values={})=>labels[key].replace(/\{(\w+)\}/g,(token,name)=>String(values[name]??token));
document.title=labels.title+' · Trail Mate';
for (const node of document.querySelectorAll('[data-label]')) node.textContent = labels[node.dataset.label];
for(const node of document.querySelectorAll('[data-geo-aria]'))node.setAttribute('aria-label',labels[node.dataset.geoAria]);
const $ = id => document.getElementById(id);
const presentation=createLocalizedStatus({getText:()=>({...labels,...statuses}),getNode:$,formatValue:value=>typeof value==='number'||typeof value==='bigint'?new Intl.NumberFormat(locale).format(value):String(value)});
presentation.set('connection-state','offline');presentation.set('directory-status','noDirectory');presentation.set('notice','start');
const showNotice=(key,values={},suffix='')=>presentation.set('notice',key,values,suffix);
let worker = new Worker(new URL('./reticulum-worker.js', import.meta.url), {type:'module'});
let nextId = 0, ready = false, rows = [], detailId = null, detailRevision = null, detailHash = null, detailGeneration = 0, partial = null;
const pending = new Map(), selected = new Set();
const map = L.map('map', {worldCopyJump:true, minZoom:0}).fitWorld();
const tiles = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom:19, updateWhenIdle:true,
  keepBuffer:1, attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'}).addTo(map);
const markers = L.layerGroup().addTo(map);
let draftMarker=null;
function selectDraftCoordinate(event){
  if(document.body.dataset.geoPick!=='true')return;
  window.dispatchEvent(new CustomEvent('trailmate:geo-coordinate',{detail:{latitude:event.latlng.lat,longitude:event.latlng.lng}}));
}
function previewDraftCoordinate(event){
  if(draftMarker){map.removeLayer(draftMarker);draftMarker=null;}
  const point=event.detail;
  if(point&&Number.isFinite(point.latitude)&&Number.isFinite(point.longitude)&&Math.abs(point.latitude)<=90&&point.longitude>=-180&&point.longitude<180){
    draftMarker=L.circleMarker([point.latitude,point.longitude],{radius:8,color:'#0c8d81',fillColor:'#f9d87a',fillOpacity:.9,interactive:false,className:'geo-draft-marker'}).addTo(map);
  }
}
map.on('click',selectDraftCoordinate);
window.addEventListener('trailmate:geo-preview',previewDraftCoordinate);
const boundaryLayer = L.geoJSON(null, {style:{color:'#0c8d81',weight:2,fillOpacity:0.07},interactive:false}).addTo(map);
const regionData = new Map();
let regionFeature = null, regionLoad = 0, querySerial = 0, manualArea = false;
let lastQuery = null;
const liveRefresh = createLiveRefresh({refresh:()=>search({automatic:true}),
  canRefresh:()=>ready && pending.size===0 && lastQuery!==null && navigator.onLine,
  isVisible:()=>!document.hidden && navigator.onLine});
const regionName = feature => chinese ? (feature.properties.zh || feature.properties.name) : feature.properties.name;

function populateRegions() {
  const features = regionData.get($('region-kind').value) || [];
  const matched = features.filter(feature=>countryMatches(feature,$('region-search').value));
  const options = matched.sort((a,b)=>regionName(a).localeCompare(regionName(b),locale)).map(feature=>{
    const option = document.createElement('option'); option.value=feature.id;
    option.textContent=regionName(feature); return option;
  });
  const placeholder = document.createElement('option'); placeholder.value='';
  placeholder.textContent=matched.length?labels.chooseRegion:statusText('noMatches');
  $('region-select').replaceChildren(placeholder,...options);
  $('region-select').value=regionFeature?.id || '';
}

async function changeRegionKind() {
  manualArea=true; ++querySerial;
  lastQuery=null;
  const kind=$('region-kind').value, generation=++regionLoad;
  regionFeature=null; boundaryLayer.clearLayers(); $('region-search').value='';
  $('search').querySelector('[data-label]').textContent=labels.search;
  presentation.raw('map-state','');
  rows=[]; selected.clear(); renderRows(); updateSelection(); $('more').hidden=true;
  $('region-picker').hidden=kind==='map'; $('region-select').disabled=true;
  presentation.raw('region-state','');
  if (kind==='map') return;
  presentation.set('region-state','boundariesLoading');
  try {
    if (!regionData.has(kind)) {
      const response=await fetch(new URL(`../data/${kind}.json`,import.meta.url));
      if (!response.ok) throw Error('Boundary data unavailable');
      const data=await response.json(); regionData.set(kind,data.features);
    }
    if (generation!==regionLoad) return;
    populateRegions(); $('region-select').disabled=false;
    presentation.set('region-state',kind==='seas'?'seaBoundary':'landBoundary');
  } catch {
    if(generation===regionLoad)presentation.set('region-state','boundaryError');
  }
}

function selectRegion() {
  manualArea=true; ++querySerial;
  lastQuery=null;
  regionFeature=(regionData.get($('region-kind').value)||[]).find(feature=>feature.id===$('region-select').value)||null;
  boundaryLayer.clearLayers();
  if (regionFeature) {
    const [south,west,north,east]=countryBounds(regionFeature);
    boundaryLayer.addData(regionDisplayGeometry(regionFeature,(west+east)/2));
    map.fitBounds([[south,west],[north,east]],{padding:[20,20],maxZoom:9});
    presentation.set('map-state','searchRegionName',{name:regionName(regionFeature)});
  }
  $('search').querySelector('[data-label]').textContent=regionFeature?labels.searchRegion:labels.search;
  rows=[]; selected.clear(); renderRows(); updateSelection(); $('more').hidden=true;
}
tiles.on('tileerror',()=>presentation.set('map-state','tileError'));

function rpc(command, args = {}) {
  if(pending.size>=8)return Promise.reject(Error(statusText('requestBusy')));
  const id = ++nextId;
  return new Promise((resolve, reject) => {
    pending.set(id, {resolve,reject});
    try { worker.postMessage({id,command,args}); }
    catch (error) { pending.delete(id); reject(error); }
  });
}
function rejectPending(message) {
  const actions = [...pending.values()]; pending.clear();
  for (const action of actions) action.reject(Error(message));
}
const notice=message=>presentation.raw('notice',message);
const bounds = () => { const b = map.getBounds(); return [b.getSouth(), b.getWest(), b.getNorth(), b.getEast()]; };
const stateMask = () => [...document.querySelectorAll('input[name=state]:checked')].reduce((mask,input) => mask | Number(input.value), 0);

async function search({automatic=false} = {}) {
  if (automatic && !lastQuery) return;
  const mask = automatic ? lastQuery.stateMask : stateMask();
  if(!mask){showNotice('chooseState');return;}
  if(!automatic&&$('region-kind').value!=='map'&&!regionFeature){showNotice('chooseRegion');return;}
  if (!automatic) {
    lastQuery={bounds:regionFeature?countryBounds(regionFeature):bounds(),stateMask:mask,region:regionFeature};
    rows=[]; renderRows(); $('more').hidden=true;
    selected.clear(); updateSelection();
  }
  const queryToken=++querySerial;
  if(!automatic)showNotice('querying');
  try { await rpc('query', {...lastQuery,queryToken,refresh:automatic}); }
  catch (error) { if(queryToken===querySerial) notice(error.message); }
  finally { liveRefresh.touch(); }
}

function updateSelection() {
  $('selection-count').textContent = selected.size ? label('selectedCount',{count:new Intl.NumberFormat(locale).format(selected.size)}) : '';
  $('download-selected').disabled = !selected.size || !ready;
}

function renderRows() {
  $('result-count').textContent = label('loadedCount',{count:new Intl.NumberFormat(locale).format(rows.length)});
  const items = rows.map(row => {
    const item = document.createElement('li'), select = document.createElement('input'), button = document.createElement('button');
    select.type = 'checkbox'; select.checked = selected.has(row.id); select.disabled = row.conflict;
    select.setAttribute('aria-label',label('selectCache',{name:row.summary[6]}));
    select.onchange = () => {
      if(select.checked&&selected.size>=20){select.checked=false;showNotice('selectionLimit');return;}
      if (select.checked) selected.add(row.id); else selected.delete(row.id);
      updateSelection();
    };
    button.className = 'result'; button.textContent = row.summary[6];
    const subtitle = document.createElement('small');
    subtitle.textContent = row.conflict ? labels.sourceConflict :
      `${[labels.active,labels.disabled,labels.archived][row.summary[3]]} · D ${row.summary[7]/2} / T ${row.summary[8]/2} · v${row.summary[1]}`;
    button.append(subtitle); button.onclick = () => openDetail(row);
    item.append(select,button); return item;
  });
  $('results').replaceChildren(...items); renderMarkers();
}

function renderMarkers() {
  markers.clearLayers();
  const cells = new Map(), center = map.getCenter().lng;
  for (const row of rows) {
    const lat = row.summary[4]/1e7, rawLon = row.summary[5]/1e7;
    const lon = rawLon + 360 * Math.round((center - rawLon)/360);
    const pixel = map.latLngToContainerPoint([lat,lon]), key = `${Math.floor(pixel.x/50)},${Math.floor(pixel.y/50)}`;
    if (!cells.has(key)) cells.set(key, []);
    cells.get(key).push({row,lat,lon});
  }
  for (const cluster of cells.values()) {
    const {row,lat,lon} = cluster[0], multi = cluster.length > 1;
    const content = document.createElement('span'), image = document.createElement('img');
    image.src = new URL(cacheIconUrl, import.meta.url).href; image.alt = ''; image.width = image.height = 32;
    content.append(image);
    if (multi) {
      const count = document.createElement('span');
      count.className = 'cache-marker-count'; count.textContent = String(cluster.length);
      content.append(count);
    }
    const icon = L.divIcon({className:`cache-marker ${multi?'cluster':row.conflict?'conflict':row.summary[3]?'inactive':''}`,
      html:content,iconSize:[40,40],iconAnchor:[20,20]});
    const marker = L.marker([lat,lon],{icon,title:multi?`${cluster.length} caches`:row.summary[6]}).addTo(markers);
    marker.on('click', () => multi ? map.setView([lat,lon],Math.min(19,map.getZoom()+2)) : openDetail(row));
  }
}

async function openDetail(row) {
  const generation = ++detailGeneration; detailId = row.id; detailRevision=row.summary[1]; detailHash=row.summary[2];
  $('detail').hidden = false; $('detail-name').textContent = row.summary[6];
  $('detail-coordinates').textContent = `${(row.summary[4]/1e7).toFixed(7)}, ${(row.summary[5]/1e7).toFixed(7)}`;
  $('detail-coordinates').dir='ltr';
  detailPresentation=null;$('detail-content').replaceChildren(); $('download-one').disabled = true;
  presentation.set('detail-state','verifyingDetails');
  try {
    const item = await rpc('get',{cacheId:row.id});
    if (generation !== detailGeneration) return;
    presentation.set('detail-state',item.isCurrent?'signatureVerified':'historicalVerified');
    const description = document.createElement('p'); description.textContent = item.record[9];
    const hint = document.createElement('details'), summary = document.createElement('summary'), text = document.createElement('p');
    text.textContent=item.record[10];hint.append(summary,text);
    const metadata = document.createElement('p'); metadata.className = 'fingerprint';
    detailPresentation={item,summary,text,metadata};refreshDetailLabels();
    $('detail-content').append(description,hint,metadata); $('download-one').disabled = false;
  }catch(error){if(generation===detailGeneration)presentation.raw('detail-state',error.message);}
}
let detailPresentation=null;
function refreshDetailLabels(){
  if(!detailPresentation)return;
  const {item,summary,text,metadata}=detailPresentation;
  summary.textContent=statusText('showHint');if(!item.record[10])text.textContent=statusText('noHint');
  metadata.textContent=`${statusText('author')}: ${item.authorHash}\n${statusText('version')}: ${new Intl.NumberFormat(locale).format(item.record[3])}\n${statusText('verifiedAt')}: ${new Date(item.checkedAt).toLocaleString(locale)}\n${item.sourceName}`;
}

function saveGpx(gpx, filename) {
  const url = URL.createObjectURL(new Blob([gpx],{type:'application/gpx+xml;charset=utf-8'}));
  const link = document.createElement('a'); link.href=url; link.download=filename; link.click();
  setTimeout(() => URL.revokeObjectURL(url),10000);
}

async function download(cacheIds) {
  showNotice('fetching');
  try {
    const result = await rpc('download',{cacheIds});
    const filename = cacheIds.length===1?`${cacheIds[0]}.gpx`:'trail-mate-geocaches.gpx';
    if (result.failures.length) {
      partial = result.gpx ? {gpx:result.gpx,filename} : null;
      $('download-partial').hidden = !partial;
      showNotice('downloadPartial',{succeeded:result.succeeded,failed:result.failures.length},result.failures.map(f=>`${f.cacheId.slice(0,12)}: ${f.message}`).join('\n'));
    } else {
      saveGpx(result.gpx,filename);showNotice('gpxSaved');
    }
  } catch (error) { notice(error.message); }
}

function receiveWorkerMessage({data}) {
  if (data.id) {
    const action=pending.get(data.id); if (!action) return; pending.delete(data.id);
    if (data.error) action.reject(Object.assign(Error(data.error),{code:data.errorCode??undefined,status:data.errorStatus??undefined,reason:data.errorReason??undefined})); else action.resolve(data.result);
    return;
  }
  const event=data.event;
  if (event.type==='status') {
    presentation.set('connection-state',event.state==='disconnected'?'disconnected':'discovering');
    if (event.state==='disconnected') {
      ready=false; ++querySerial; ++detailGeneration; rows=[]; selected.clear(); partial=null; detailId=null;
      liveRefresh.setReady(false);
      $('detail').hidden=true; $('more').hidden=true; $('download-partial').hidden=true;
      $('search').disabled=true; $('region-apply').disabled=true; $('connection-state').classList.remove('ready');
      renderRows(); updateSelection();
      detailPresentation=null;showNotice('connectionLost');
    }
  } else if (event.type==='directory') {
    const first = !ready; ready=true; $('search').disabled=false; $('region-apply').disabled=false;
    liveRefresh.setReady(true);
    presentation.set('connection-state','ready');$('connection-state').classList.add('ready');
    presentation.set('directory-status','verifiedDirectories',{count:event.count});
    if (first) initialLocation.finally(()=>{if(ready)search();});
  } else if (event.type==='results') {
    if (!ready || event.queryToken!==querySerial) return;
    rows=event.rows; $('more').hidden=!event.more;
    const available=new Map(rows.map(row=>[row.id,row]));
    for (const id of selected) if (!available.has(id) || available.get(id).conflict) selected.delete(id);
    if (detailId && !available.has(detailId)) {
      ++detailGeneration;detailPresentation=null;detailId=null;$('detail').hidden=true;
    } else if (detailId && (available.get(detailId).conflict || available.get(detailId).summary[1]!==detailRevision ||
      available.get(detailId).summary[2].some((byte,index)=>byte!==detailHash[index]))) {
      ++detailGeneration;
      detailPresentation=null;presentation.set('detail-state','cacheUpdated');
      $('download-one').disabled=true;
    }
    renderRows(); updateSelection();
    showNotice(event.limited?'displayLimit':!rows.length&&event.more?'noMatchesPages':'resultsLoaded');
  } else if (event.type==='source-error') notice(`${event.name}: ${event.message}`);
}
function recoverWorker() {
  if (serviceStopped) return;
  serviceStopped=true;
  receiveWorkerMessage({data:{event:{type:'status',state:'disconnected'}}});
  clearTimeout(serviceRetry); worker.terminate();
  rejectPending(statusText('interrupted'));
  $('search').disabled=true; $('region-apply').disabled=true; updateSelection();
  showNotice('interrupted');
  const restart=()=>{
    try {
      worker=new Worker(new URL('./reticulum-worker.js',import.meta.url),{type:'module'});
    } catch {
      serviceRetry=setTimeout(restart,5000);
      return;
    }
    worker.onmessage=receiveWorkerMessage; worker.onerror=recoverWorker;
    serviceStopped=false; startService();
  };
  serviceRetry=setTimeout(restart,5000);
}
worker.onmessage=receiveWorkerMessage; worker.onerror=recoverWorker;
let serviceRetry = null, serviceStarting = false, serviceStopped = false, serviceRetryDelay = 5000;
  registerGeocachingNetwork({rpc,isReady:()=>ready&&!serviceStopped});
async function startService() {
  if (serviceStarting || serviceStopped) return;
  serviceStarting = true;
  clearTimeout(serviceRetry); serviceRetry = null;
  try {
    const response = await fetch(new URL('../network.json', import.meta.url), {cache:'no-store',signal:AbortSignal.timeout(15000)});
    if (!response.ok) throw Error('Missing deployment configuration');
    const config = await response.json();
    const url = new URL(config.endpoint);
    if (url.protocol !== 'wss:' && !(url.protocol === 'ws:' && ['localhost','127.0.0.1','[::1]'].includes(url.hostname))) throw Error('Invalid deployment endpoint');
    if (serviceStopped) return;
    await rpc('connect',{url:url.href,discoverySeeds:config.discoverySeeds});
    serviceRetryDelay = 5000;
  } catch {
    if (serviceStopped) return;
    presentation.set('connection-state','unavailable');presentation.set('directory-status','retryLater');showNotice('retrying');
    serviceRetry = setTimeout(startService, serviceRetryDelay);
    serviceRetryDelay = Math.min(60000, serviceRetryDelay * 2);
  } finally {
    serviceStarting = false;
  }
}
$('search').onclick=()=>{manualArea=true;return search();};
$('region-apply').onclick=()=>{manualArea=true;return search();};
$('region-kind').onchange=changeRegionKind;
$('region-search').oninput=populateRegions;
$('region-select').onchange=selectRegion;
$('more').onclick=async () => { $('more').disabled=true; try { await rpc('more'); } catch(error) {notice(error.message);} finally {$('more').disabled=false;} };
$('download-one').onclick=() => detailId && download([detailId]);
$('download-selected').onclick=() => download([...selected]);
$('download-partial').onclick=() => {if(partial)saveGpx(partial.gpx,partial.filename);partial=null;$('download-partial').hidden=true;};
$('close-detail').onclick=() => {++detailGeneration;$('detail').hidden=true;};
async function locateNearby(explicit=false) {
  const serial=querySerial;
  presentation.set('map-state','locationPrompt');
  try {
    const [south,west,north,east]=await requestNearbyPosition(navigator.geolocation);
    if (serial!==querySerial || (!explicit && manualArea)) return;
    if (explicit) { $('region-kind').value='map'; await changeRegionKind(); }
    map.fitBounds([[south,west],[north,east]],{padding:[20,20],maxZoom:12});
    presentation.set('map-state','locationShown');
    if (explicit && ready) await search();
  } catch {
    if(serial===querySerial)presentation.set('map-state','locationError');
  }
}
$('locate').onclick=()=>locateNearby(true);
map.on('dragstart zoomstart',event=>{if(event.originalEvent)manualArea=true;});
map.on('dragstart',()=>{manualArea=true;});
map.getContainer().addEventListener('wheel',()=>{manualArea=true;},{passive:true});
const initialLocation=locateNearby();
startService();
map.on('moveend',renderMarkers);
document.addEventListener('visibilitychange',()=>{if(!document.hidden)liveRefresh.resume();});
window.addEventListener('online',()=>liveRefresh.resume());
window.addEventListener('pagehide',()=>{liveRefresh.stop();serviceStopped=true;clearTimeout(serviceRetry);worker.terminate();rejectPending('Page closed');});
// Back/forward cache restores the document but cannot revive a terminated
// worker. Reconnect through normal startup instead of leaving dead RPCs.
window.addEventListener('pageshow',event=>{if(event.persisted)window.location.reload();});
window.addEventListener('keydown',event=>{if(event.key==='Escape')$('close-detail').click();});
const languageRuntime=await getManagementLanguageRuntime();
await languageRuntime.watchManagementLanguage(snapshot=>{
  if(snapshot.locale===locale)return;
  locale=snapshot.locale;chinese=locale.startsWith('zh');labels=geocachingBrowseText(locale);statuses=geocachingStatusText(locale);
  document.title=labels.title+' · Trail Mate';
  const dynamicIds=new Set(['connection-state','directory-status','notice','result-count']);
  for(const node of document.querySelectorAll('[data-label]'))if(!dynamicIds.has(node.id))node.textContent=labels[node.dataset.label];
  for(const node of document.querySelectorAll('[data-geo-aria]'))node.setAttribute('aria-label',labels[node.dataset.geoAria]);
  if(regionFeature||$('region-kind').value!=='map')populateRegions();
  $('search').querySelector('[data-label]').textContent=regionFeature?labels.searchRegion:labels.search;
  if(regionFeature&&presentation.get('map-state')?.key==='searchRegionName')presentation.set('map-state','searchRegionName',{name:regionName(regionFeature)});
  presentation.refresh();refreshDetailLabels();
  renderRows();updateSelection();
});
