// Presentation and input constraints for TMSET7. Firmware performs the final
// cross-field/platform validation; unknown fields remain visible and preserved.
import {isConfigurationTextBlob,validateConfigurationTextBlob,AUTO_REPLY_MAX_BYTES} from './config-text-blob.js';
import {C6_CONFIG_FIELDS} from './config-c6-validation.js';
import {configTimezoneText} from '../../i18n/config-timezone-labels.js';
import {configMeaningText} from '../../i18n/config-meaning-labels.js';
import {radioRegionOptions} from './radio-region-options.js';
import {meshCoreRegionOptions,MESHCORE_REGION_PRESETS} from './meshcore-region-presets.js';
import {configMeshCoreRegionText} from '../../i18n/config-meshcore-region-labels.js';
import {configCharacterText} from '../../i18n/config-character-labels.js';
import {configurationCharacterChoices} from './config-character.js';
import {configurationDuration} from './config-duration.js';
import {isChannelPositionPrecision,positionPrecisionOptions} from './config-position-precision.js';
import {CAPABILITY} from './hostlink.js';
import {configPositionRuntimeText} from '../../i18n/config-position-runtime-labels.js';
import {configPrecisionText} from '../../i18n/config-precision-labels.js';
import {configurationRadioTuning} from './config-radio-tuning.js';
import {configReticulumInterfaceText,interfaceAccessLengthOptions} from '../../i18n/config-reticulum-interface-labels.js';
import {configurationFrequency} from './config-frequency.js';
import {configurationRadioValue} from './config-radio-values.js';
import {configurationNumericPurpose} from './config-numeric-purpose.js';
const names={policy:'通信策略',device:'设备',channel:'频道',gps:'定位',map:'地图',route:'路线',chat:'聊天',network:'网络',privacy:'隐私',aprs:'APRS',mt:'Meshtastic',mc:'MeshCore',rt:'Reticulum',mqtt:'MQTT',base:'无线',net:'网络',interface:'接口',propagation:'传播',group:'群组',wifi:'Wi-Fi',ui:'界面',screen:'屏幕',display:'显示',power:'电源',cellular:'蜂窝通信',mt_ble:'Meshtastic 蓝牙',mc_ble:'MeshCore 蓝牙',mt_module:'Meshtastic 模块',schema:'格式',document:'文档'};
const fields={enabled:'启用',relay:'转发',hop_limit:'跳数上限',ack_broadcast:'广播确认',ack_squad:'小队确认',max_retries:'最多重试次数',max_channels:'最多频道数',active:'当前协议',node_name:'设备名称',short_name:'短名称',ble_enabled:'启用蓝牙',primary_enabled:'启用主频道',secondary_enabled:'启用副频道',primary_uplink:'主频道上行',primary_downlink:'主频道下行',secondary_uplink:'副频道上行',secondary_downlink:'副频道下行',primary_has_module:'主频道模块设置',secondary_has_module:'副频道模块设置',primary_position_precision:'主频道定位精度',secondary_position_precision:'副频道定位精度',primary_muted:'主频道静音',secondary_muted:'副频道静音',secondary_psk:'副频道密钥',primary_psk:'主频道密钥',init_baud:'初始化波特率',init_probe_ms:'初始化探测时间（毫秒）',init_profile:'初始化配置',init_rxm_policy:'接收策略',init_gnss_policy:'卫星系统策略',init_nmea_policy:'NMEA 策略',interval_ms:'更新间隔（毫秒）',mode:'模式',sat_mask:'卫星系统掩码',strategy:'定位策略',alt_ref:'高度参考',coord_format:'坐标格式',motion_idle_ms:'静止判定时间（毫秒）',motion_sensor:'运动传感器',external_nmea_hz:'外部 NMEA 输出（Hz）',external_nmea_mask:'NMEA 语句掩码',coord_system:'坐标系',source:'来源',contour_enabled:'等高线',track_enabled:'轨迹记录',track_interval:'记录间隔',track_format:'记录格式',channel:'默认频道',duty_cycle:'占空比限制',channel_util:'信道使用率限制',encrypt_mode:'加密模式',path:'路径',igate_callsign:'网关呼号',igate_ssid:'网关 SSID',tocall:'目标呼号',tx_min_interval_s:'最短发送间隔（秒）',dedupe_window_s:'去重窗口（秒）',symbol_table:'符号表',symbol_code:'符号代码',position_interval_s:'位置发送间隔（秒）',node_map:'节点呼号映射',self_enabled:'发送自身位置',self_callsign:'自身呼号',region:'区域',use_preset:'使用无线预设',modem_preset:'调制预设',bandwidth_khz:'带宽（kHz）',spread_factor:'扩频因子',coding_rate:'编码率',tx_power:'发送功率（dBm）',tx_enabled:'启用发送',override_duty_cycle:'覆盖占空比限制',channel_num:'信道编号',frequency_offset_mhz:'频率偏移（MHz）',override_frequency_mhz:'自定义频率（MHz）',enable_relay:'启用转发',ignore_mqtt:'忽略 MQTT',config_ok_to_mqtt:'允许通过 MQTT',primary_channel_name:'主频道名称',secondary_channel_name:'副频道名称',primary_channel_id:'主频道 ID',secondary_channel_id:'副频道 ID',uplink:'上行',downlink:'下行',host:'服务器地址',port:'端口',root:'主题根路径',username:'用户名',password:'密码',region_preset:'区域预设',frequency_mhz:'频率（MHz）',client_repeat:'客户端转发',rx_delay_base:'接收延迟基数',airtime_factor:'空中时间系数',flood_max:'洪泛上限',multi_acks:'多次确认',send_profile:'发送策略',forward_profile:'转发策略',active_slot:'当前槽位',name:'名称',key:'密钥',lora_enabled:'启用 LoRa',wifi_enabled:'启用 Wi-Fi',wifi_auto_connect:'自动连接 Wi-Fi',anonymous_peer:'匿名节点',wifi_host:'网关地址',wifi_port:'网关端口',interface_policy:'接口策略',allow_location_requests:'允许位置请求',identity_valid:'身份有效',destination_hash:'目的地址',identity_hash:'身份哈希',version:'版本',interface_count:'接口数量',id:'稳定标识',type:'类型',target_host:'服务器地址',target_port:'服务器端口',group_id:'接口组',discovery_port:'发现端口',data_port:'数据端口',network_name:'网络名称',passphrase:'网络口令',ifac_size_bits:'IFAC 长度（位）',service_enabled:'启用传播服务',delivery:'投递策略',automatic_node:'自动选择节点',node_hash:'传播节点地址',sync_on_start:'启动时同步',sync_interval_s:'同步间隔（秒）',max_messages_per_sync:'每次最多同步消息数',timeout_ms:'超时（毫秒）',brightness:'亮度',volume:'音量',vibration:'振动',locale:'语言',enabled_imes:'启用输入法',profile:'配置档',offset_minutes:'时区偏移（分钟）',tzdef:'时区定义',message_alerts:'消息提醒',contact_alerts:'联系人提醒',auto_reply_enabled:'自动回复',auto_reply_text:'自动回复内容',debug_logs:'调试日志',gauge_design_mah:'设计容量（mAh）',gauge_full_mah:'满充容量（mAh）',count:'数量',ssid:'网络名称（SSID）',fixed_pin:'固定配对 PIN',pin:'配对 PIN',manual_add_contacts:'手动添加联系人',telemetry_base:'基础遥测',telemetry_location:'位置遥测',telemetry_environment:'环境遥测',advert_location_policy:'广播位置策略',format:'格式',size:'字节数',chunk_count:'块数量',smtp_port:'SMTP 端口',smtp_host:'SMTP 地址',smtp_user:'SMTP 用户名',smtp_password:'SMTP 密码',smtp_from:'发件地址',smtp_default_recipient:'默认收件地址',smtp_security:'SMTP 安全模式',apn:'APN',apn_user:'APN 用户名',apn_password:'APN 密码',smsc:'短信中心',auto_answer:'自动接听',speaker_gain:'扬声器增益',microphone_gain:'麦克风增益'};
const ranges={u8:[0,255],i8:[-128,127],u16:[0,65535],u32:[0,4294967295],i32:[-2147483648,2147483647]};
export function configurationField(record,presentation,document,{firmwareCapabilities=0}={}){
 const parts=record.key.split('.'),last=parts.at(-1);
 const lookup=(map,key)=>map&&Object.hasOwn(map,key)?map[key]:undefined;
 const prefix=parts.slice(0,-1).map(part=>/^\d+$/.test(part)?(presentation?.index?.(Number(part)+1)??String(Number(part)+1)):lookup(presentation?.names,part)??lookup(names,part)??part).join(' / ');
 const label=lookup(presentation?.fields,last)??lookup(fields,last)??last;
 const result={label:prefix?`${prefix} · ${label}`:label,type:record.type,secret:/(password|passphrase|psk|private|secret|\.key$|_pin$|\.pin$)/i.test(record.key),readOnly:['schema.version','document.kind','rt.net.version'].includes(record.key),range:ranges[record.type]};
 const settingRanges={'ui.display.brightness':[0,255],'ui.audio.speaker_volume':[0,100],'ui.timezone.offset_minutes':[-720,840],'chat.alerts.message':[0,1],'chat.alerts.contact':[0,2],'power.gauge.design_mah':[1,10000],'power.gauge.full_mah':[1,10000]};
 if(Object.hasOwn(settingRanges,record.key))result.range=settingRanges[record.key];
 if(record.key==='ui.timezone.profile'){result.options=presentation?.timezoneProfiles??configTimezoneText('zh-Hans').timezoneProfiles;result.help=presentation?.timezoneProfileHelp;}
 if(record.key==='ui.timezone.offset_minutes')result.help=presentation?.timezoneOffsetHelp;
 if(record.key==='ui.screen.timeout_ms')result.options=[15000,30000,60000,300000].map(value=>({value,label:value===300000?(presentation?.always??'保持常亮'):(presentation?.seconds?.(value/1000)??`${value/1000} s`)}));
 if(record.key==='mt_ble.mode')result.options=[[0,presentation?.randomPin??'随机 PIN'],[1,presentation?.fixedPin??'固定 PIN'],[2,presentation?.noPin??'无 PIN']].map(([value,label])=>({value,label}));
 if(['mt_ble.fixed_pin','mc_ble.pin'].includes(record.key)){result.range=[0,999999];result.help=presentation?.pinHelp??'输入 0，或 100000–999999 的六位 PIN。';}
 if(/^mc_ble\.telemetry_(base|location|environment)$/.test(record.key))result.range=[0,3];
 if(['rt.net.interface_count','wifi.profile_count'].includes(record.key))result.readOnly=true;
 if(record.key==='mc.channel.0.enabled'){result.readOnly=true;result.help=presentation?.meshCorePrimaryHelp??'MeshCore 第一个频道始终启用，可以修改它的名称和密钥。';}
 if(record.key==='mc.base.secondary_psk'){result.readOnly=true;result.help=presentation?.meshCoreMirrorHelp??'此兼容值来自当前 MeshCore 频道。请修改对应频道的密钥。';}
 if(record.key==='mc.active_slot')result.options=presentation?.meshCoreSlots??Array.from({length:8},(_,value)=>({value,label:`频道 ${value+1}`}));
 if(/^rt\.net\.interface\.\d+\.ifac_size$/.test(record.key)){const locale=presentation?.locale??'zh-Hans',text=configReticulumInterfaceText(locale);result.label=`${prefix} · ${presentation?.ifacBits??'IFAC 长度（位）'}`;result.range=[0,512];result.step=8;result.interfaceAccessLength=true;result.options=interfaceAccessLengthOptions(locale);result.allowCustom=true;result.customLabel=text.custom;result.help=text.bitsHelp;}
 const textLimits=[[/^device\.node_name$/,31],[/^device\.short_name$/,15],
  [/^(mt|mc)\.mqtt\.(host|root|username|password)$/,63],
  [/^(mt|rt|mc\.base)\.(primary|secondary)_channel_name$/,31],
  [/^mc\.channel\.\d+\.name$/,31],[/^rt\.group\.\d+\.name$/,31],[/^rt\.wifi_host$/,63],
  [/^aprs\.(igate_callsign|tocall|self_callsign)$/,15],[/^aprs\.path$/,63],[/^route\.path$/,95],
  [/^wifi\.profile\.\d+\.ssid$/,32],[/^wifi\.profile\.\d+\.password$/,64],
  [/^rt\.net\.interface\.\d+\.id$/,23],[/^rt\.net\.interface\.\d+\.(target_host|network_name|passphrase)$/,63],
  [/^rt\.net\.interface\.\d+\.group_id$/,31]];
 result.maxBytes=textLimits.find(([pattern])=>pattern.test(record.key))?.[1];
 // Keep the terminating NUL reserved in the modem Config char arrays.
 const cellularTextBytes={apn:63,apn_user:47,apn_password:63,smsc:31,smtp_host:95,smtp_user:95,smtp_password:95,smtp_from:95,smtp_default_recipient:95};
 if(record.key.startsWith('cellular.')&&Object.hasOwn(cellularTextBytes,last))result.maxBytes=cellularTextBytes[last];
 if(['cellular.speaker_gain','cellular.microphone_gain'].includes(record.key))result.range=[0,15];
 if(record.key==='cellular.smtp_security')result.range=[0,2];
 if(record.key.startsWith('c6_companion.')){const spec=C6_CONFIG_FIELDS[last];if(spec){if(spec.min!==undefined)result.range=[spec.min,spec.max];if(spec.maxBytes)result.maxBytes=spec.maxBytes;}}
 if(record.key==='c6_companion.wifi_ap_password')result.help=presentation?.c6ApPasswordHelp;
 if(isConfigurationTextBlob(record.key))result.maxBytes=AUTO_REPLY_MAX_BYTES;
 if(record.key==='protocol.active')result.options=[{value:1,label:'Meshtastic'},{value:2,label:'MeshCore'},{value:4,label:'Reticulum'}];
 const policyChoices={
  'rt.interface_policy':presentation?.reticulumPolicies??['全部接口','仅 LoRa','仅 Wi-Fi 网关'],
  'mc.send_profile':presentation?.meshCoreSendProfiles??['仅 V1','自动 · 优先 V2','仅 V2'],
  'mc.forward_profile':presentation?.meshCoreForwardProfiles??['允许所有格式','仅多字节路径'],
 };
 if(Object.hasOwn(policyChoices,record.key))result.options=policyChoices[record.key].map((label,value)=>({value,label}));
 if(record.key==='rt.interface_policy')result.help=presentation?.reticulumPolicyHelp;
 if(/^(mt|rt|mc\.base)\.modem_preset$/.test(record.key))result.options=[[0,'LongFast'],[9,'LongTurbo'],[7,'LongModerate'],[1,'LongSlow'],[4,'MediumFast'],[3,'MediumSlow'],[6,'ShortFast'],[5,'ShortSlow'],[8,'ShortTurbo']].map(([value,label])=>({value,label}));
 if(/^rt\.net\.interface\.\d+\.type$/.test(record.key))result.options=[{value:0,label:presentation?.integratedLoRa??'集成 LoRa'},{value:1,label:'AutoInterface'},{value:2,label:presentation?.tcpClient??'TCP 客户端'}];
 if(record.key==='rt.net.propagation.delivery')result.options=[{value:0,label:presentation?.direct??'直连'},{value:1,label:presentation?.propagated??'传播'},{value:2,label:presentation?.automatic??'自动'}];
 const choices={
  'gps.mode':[[0,presentation?.highAccuracy??'高精度'],[1,presentation?.powerSave??'省电'],[2,presentation?.fixOnly??'仅定位']],
  'gps.coord_format':[[0,'DD'],[1,'DMS'],[2,'UTM']],
  'gps.external_nmea_hz':[[0,presentation?.off??'关闭'],[1,'1 Hz'],[5,'5 Hz']],
  'gps.external_nmea_mask':[[0,'GGA + RMC + GSA + GSV'],[1,'RMC + GSA + GSV'],[2,'GGA + RMC']],
  'map.coord_system':[[0,'WGS84'],[1,'GCJ-02'],[2,'BD-09']],
  'map.source':[[0,'OSM'],[1,presentation?.terrain??'地形'],[2,presentation?.satellite??'卫星']],
  'map.track_interval':[[1,presentation?.seconds?.(1)??'1 秒'],[5,presentation?.seconds?.(5)??'5 秒'],[10,presentation?.seconds?.(10)??'10 秒'],[99,presentation?.distance??'按距离']],
  'map.track_format':[[0,'GPX'],[1,'CSV'],[2,presentation?.binary??'二进制']],
 };
 if(Object.hasOwn(choices,record.key))result.options=choices[record.key].map(([value,label])=>({value,label}));
 const meaning=presentation?.meaning??configMeaningText('zh-Hans');
 if(isChannelPositionPrecision(record.key)&&record.type==='u32'){const locale=presentation?.locale??'zh-Hans',text=presentation?.precision??configPrecisionText(locale);result.positionPrecision=true;result.options=positionPrecisionOptions(locale);result.allowCustom=true;result.customLabel=text.custom;result.help=text.meaning+' '+((firmwareCapabilities&CAPABILITY.CHANNEL_POSITION_PRECISION)?configPositionRuntimeText(locale):text.channelScope);}
 if(/^channel\.(primary|secondary)_has_module$/.test(record.key))result.help=(presentation?.precision??configPrecisionText('zh-Hans')).moduleFlagHelp;
 const semanticChoices={
  'chat.channel':['primary','secondary'], 'privacy.encrypt_mode':['off','psk','pki'],
  'chat.alerts.message':['off','on'], 'chat.alerts.contact':['off','contacts','all'],
  'gps.strategy':['continuous','motion','lowPower'], 'gps.alt_ref':['sea','ellipsoid'],
  'gps.init_profile':['auto','passive','legacy','modern'],
  'gps.init_rxm_policy':['auto','skip','send'], 'gps.init_gnss_policy':['auto','skip','send'], 'gps.init_nmea_policy':['auto','skip','send'],
  'gps.coord_format':['decimal','dms','utm'],
 };
 if(Object.hasOwn(semanticChoices,record.key))result.options=semanticChoices[record.key].map((key,value)=>({value,label:meaning[key]}));
 if(record.key==='chat.channel'){
  const configured=key=>{try{return document?.get(key)?.value;}catch{return undefined;}},prefix={1:'mt',2:'mc.base',4:'rt'}[configured('protocol.active')];
  if(prefix)result.options=result.options.map((option,index)=>{const name=configured(prefix+'.'+(index?'secondary':'primary')+'_channel_name');return {...option,label:typeof name==='string'&&name.trim()?option.label+' · '+name:option.label};});
 }
 const semanticHelp={'device.short_name':'shortName','chat.channel':'channel','privacy.encrypt_mode':'encryption','chat.alerts.message':'message','chat.alerts.contact':'contact'};
 if(Object.hasOwn(semanticHelp,record.key))result.help=meaning.help[semanticHelp[record.key]];
 if(record.key==='gps.init_baud'){result.options=[0,9600,38400,115200,57600,19200,4800].map(value=>({value,label:value===0?meaning.auto:String(value)+' baud'}));result.allowCustom=true;result.customLabel=meaning.custom;}
 if(record.key==='gps.motion_sensor'){const text=presentation?.characters??configCharacterText('zh-Hans');result.options=[[77,text.motion],[55,text.significant],[143,text.wake]].map(([value,label])=>({value,label}));result.allowCustom=true;result.customLabel=meaning.custom;result.help=text.sensorHelp;}
 if(/^aprs\.symbol_(table|code)$/.test(record.key)){const text=presentation?.characters??configCharacterText('zh-Hans');let table;try{table=document?.get('aprs.symbol_table')?.value;}catch{}result.options=configurationCharacterChoices(record.key,text,table);result.character=true;result.characterText=text;result.allowCustom=true;result.customLabel=meaning.custom;result.help=(record.key==='aprs.symbol_table'?text.tableHelp+' ':'')+text.characterHelp;}
 if(/^(mt|rt|mc(?:\.base)?)\.coding_rate$/.test(record.key))result.options=[5,6,7,8].map(value=>({value,label:'4/'+value}));
 if(/^(mt|rt|mc(?:\.base)?)\.spread_factor$/.test(record.key))result.options=Array.from({length:8},(_,index)=>({value:index+5,label:'SF'+(index+5)}));
 if(/^(mt|rt|mc\.base)\.region$/.test(record.key))result.options=presentation?.radioRegions??radioRegionOptions('zh-Hans',meaning.unset);
 if(record.key==='mc.region_preset'){const text=presentation?.meshCoreRegionText??configMeshCoreRegionText('zh-Hans'),preset=MESHCORE_REGION_PRESETS.find(row=>row.id===record.value);result.options=presentation?.meshCoreRegions??meshCoreRegionOptions('zh-Hans');result.help=text.help+(preset?' '+preset.frequency+' MHz · '+preset.bandwidth+' kHz · SF'+preset.sf+' · 4/'+preset.cr+' · '+preset.power+' dBm':'');}
 if(record.key==='network.channel_util'){result.options=[0,25,50].map(value=>({value,label:value===0?meaning.auto:String(value)+'%'}));result.allowCustom=true;result.customLabel=meaning.custom;}
 if(['ui.display.brightness','ui.audio.speaker_volume'].includes(record.key))result.percentScale=record.key==='ui.display.brightness'?255:100;
 if(!result.help&&result.maxBytes)result.help=meaning.help.bytes.replace('{max}',String(result.maxBytes));
 if(/(?:target_port|discovery_port|data_port|wifi_port|smtp_port|mqtt\.port)$/.test(record.key))result.range=[1,65535];
 if(record.key==='cellular.smtp_port')result.options=[25,465,587].map(value=>({value,label:String(value)}));
 if(record.key==='rt.net.interface_count')result.range=[0,6];
 if(record.key==='rt.net.propagation.max_messages_per_sync')result.range=[1,64];
 if(record.key==='rt.net.propagation.sync_interval_s')result.range=[60,86400];
 if(/^rt\.net\.interface\.\d+\.target_host$/.test(record.key))result.help=presentation?.tcpHostHelp??'TCP 接口使用主机名或 IP 地址，不包含 http://、路径或端口。';
 if(/^rt\.net\.interface\.\d+\./.test(record.key)){const text=configReticulumInterfaceText(presentation?.locale??'zh-Hans'),help={target_port:'tcpPortHelp',group_id:'groupHelp',discovery_port:'discoveryHelp',data_port:'dataHelp',network_name:'accessScope',passphrase:'accessScope'}[last];if(help)result.help=text[help];}
 if(record.key==='gps.interval_ms')result.help=presentation?.gpsIntervalHelp??'毫秒；60000 表示一分钟。';
 if(!result.readOnly&&/^(mt|rt|mc\.base)\.(primary|secondary)_psk$/.test(record.key))result.help=presentation?.CONFIG_CHANNEL_KEY_LENGTH;
 if(/^mc\.channel\.\d+\.key$/.test(record.key))result.help=presentation?.CONFIG_BINARY_LENGTH?.replace('{bytes}','16');
 if(record.key==='legacy.secondary_psk')result.help=presentation?.CONFIG_BINARY_LENGTH?.replace('{bytes}','32');
 if(record.key==='aprs.node_map'){result.maxBytes=255;result.help=presentation?.CONFIG_BINARY_MAX_BYTES?.replace('{bytes}','255');}
 if(!result.help&&!result.options&&result.range)result.help=meaning.help.range.replace('{min}',String(result.range[0])).replace('{max}',String(result.range[1]));
 result.duration=configurationDuration(record);if(result.duration)result.label=result.label.replace(/\s*[（(][^()（）]*[)）]\s*$/u,'');
 const tuning=configurationRadioTuning(record,presentation?.locale??'zh-Hans');if(tuning){result.radioTuning=true;result.options=tuning.options;result.allowCustom=true;result.customLabel=tuning.custom;result.help=tuning.help;result.runtimeTuningNote=tuning.runtimeNote;}
 result.frequency=configurationFrequency(record,presentation?.locale??'zh-Hans');if(result.frequency){result.help=result.frequency.help;result.label=result.label.replace(/\s*[（(]MHz[)）]\s*$/u,'');}
 result.radioValue=configurationRadioValue(record,presentation?.locale??'zh-Hans');if(result.radioValue){result.options=result.radioValue.options;result.allowCustom=true;result.customLabel=result.radioValue.customLabel;result.help=result.radioValue.help;}
 result.numericPurpose=configurationNumericPurpose(record,presentation?.locale??'zh-Hans');if(result.numericPurpose){const spec=result.numericPurpose;if(spec.label&&!lookup(presentation?.fields,last))result.label=prefix?`${prefix} · ${spec.label}`:spec.label;if(spec.help)result.help=spec.help;if(spec.options){result.options=spec.options;result.allowCustom=true;result.customLabel=spec.custom;}}
 return result;
}
export function validateConfigurationField(record,value){
 if(isConfigurationTextBlob(record.key))validateConfigurationTextBlob(value);
 const schema=configurationField(record);
 const reject=(code,message,params={})=>{throw Object.assign(Error(message),{code,params,field:record.key});};
 if(schema.readOnly)reject('CONFIG_READ_ONLY','此字段由设备管理，不能直接修改');
 if(ranges[record.type]&&!Number.isSafeInteger(value))reject('CONFIG_INTEGER','请输入整数');
 if(record.type==='f32'&&(!Number.isFinite(value)||!Number.isFinite(Math.fround(value))))reject('CONFIG_FLOAT','请输入有效的有限浮点数');
 if(schema.range&&(!Number.isFinite(value)||value<schema.range[0]||value>schema.range[1]))reject('CONFIG_RANGE',`请输入 ${schema.range[0]}–${schema.range[1]} 范围内的数值`,{min:schema.range[0],max:schema.range[1]});
 if(schema.interfaceAccessLength&&value%8!==0)reject('CONFIG_IFAC_BITS','请输入 0，或 8–512 范围内的 8 的倍数');
 if(schema.options&&!schema.allowCustom&&!schema.options.some(option=>option.value===value))reject('CONFIG_OPTION','请选择支持的选项');
 if(['mt_ble.fixed_pin','mc_ble.pin'].includes(record.key)&&value!==0&&(value<100000||value>999999))reject('BLE_PIN','Enter zero or a six-digit PIN');
 if(schema.maxBytes&&typeof value==='string'&&new TextEncoder().encode(value).length>schema.maxBytes)reject('CONFIG_UTF8_LIMIT',`UTF-8 内容不能超过 ${schema.maxBytes} 字节`,{max:schema.maxBytes});
 if(schema.maxBytes&&record.type==='b64'&&value instanceof Uint8Array&&value.length>schema.maxBytes)reject('CONFIG_BINARY_MAX_BYTES','二进制内容超过设备容量',{bytes:schema.maxBytes});
 if(/^rt\.group\.\d+\.(destination_hash|identity_hash)$/.test(record.key)&&(!(value instanceof Uint8Array)||value.length!==16))reject('CONFIG_BINARY_LENGTH','群组身份引用必须为 16 字节',{bytes:16});
 if(/^mc\.channel\.\d+\.key$/.test(record.key)&&(!(value instanceof Uint8Array)||value.length!==16))reject('CONFIG_BINARY_LENGTH','频道密钥必须为 16 字节',{bytes:16});
 if(record.key==='legacy.secondary_psk'&&(!(value instanceof Uint8Array)||value.length!==32))reject('CONFIG_BINARY_LENGTH','兼容密钥必须为 32 字节',{bytes:32});
 if(/^(mt|rt|mc\.base)\.(primary|secondary)_psk$/.test(record.key)&&(!(value instanceof Uint8Array)||![0,16,32].includes(value.length)))reject('CONFIG_CHANNEL_KEY_LENGTH','频道密钥必须为 0、16 或 32 字节');
 if(/\.target_host$/.test(record.key)&&typeof value==='string'&&(/[\s\/?#]/.test(value)||value.includes('://')))reject('CONFIG_HOST','请输入主机名或 IP，不包含 URL 路径');
}
