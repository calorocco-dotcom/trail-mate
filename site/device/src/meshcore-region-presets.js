// Mirrors modules/core_chat/src/infra/meshcore/mc_region_presets.cpp.
// Test against that table so the web UI cannot silently drift from firmware.
import {configMeshCoreRegionText} from '../../i18n/config-meshcore-region-labels.js';
export const MESHCORE_REGION_PRESETS=[
 [1,'AU','',915.8,250,10,5,20],[2,'AU','narrow',916.575,62.5,7,8,20],
 [3,'AU','SA / WA',923.125,62.5,8,8,20],[4,'AU','QLD',923.125,62.5,8,5,20],
 [5,'EU/GB','narrow',869.618,62.5,8,8,20],[6,'EU/GB','long',869.525,250,11,5,20],
 [7,'EU/GB','medium',869.525,250,10,5,20],[8,'CZ','narrow',869.432,62.5,7,5,20],
 [9,'EU','long',433.65,250,11,5,20],[10,'NZ','',917.375,250,11,5,20],
 [11,'NZ','narrow',917.375,62.5,7,5,20],[12,'PT','',433.375,62.5,9,6,20],
 [13,'PT','',869.618,62.5,7,6,20],[14,'CH','',869.618,62.5,8,8,20],
 [15,'US/CA','recommended',910.525,62.5,7,5,20],[16,'VN','',920.25,250,11,5,20],
 [17,'CN','',495.2,125,9,5,12],
].map(([id,region,variant,frequency,bandwidth,sf,cr,power])=>Object.freeze({id,region,variant,frequency,bandwidth,sf,cr,power}));
export function meshCoreRegionOptions(locale){const text=configMeshCoreRegionText(locale),names=new Intl.DisplayNames([locale],{type:'region'});return [{value:0,label:text.custom},...MESHCORE_REGION_PRESETS.map(row=>({value:row.id,label:row.region.split('/').map(code=>names.of(code)).join(' / ')+(row.variant?' · '+(text[row.variant]??row.variant):'')+' · '+row.frequency+' MHz'}))];}
export const MESHCORE_PRESET_FIELDS={'mc.frequency_mhz':'frequency','mc.bandwidth_khz':'bandwidth','mc.spread_factor':'sf','mc.coding_rate':'cr','mc.base.tx_power':'power'};
export const MESHCORE_CUSTOM_FIELDS=Object.keys(MESHCORE_PRESET_FIELDS).filter(key=>key!=='mc.base.tx_power');
