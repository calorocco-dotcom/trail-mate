// Optional board-owned block. Never manufacture it on a device that does not
// export it; firmware integration must expose the complete block atomically.
export const C6_CONFIG_FIELDS=Object.freeze({
 espnow_enabled:{type:'bool'},espnow_team_discovery:{type:'bool'},
 espnow_channel:{type:'u8',min:0,max:14},espnow_beacon_interval_ms:{type:'u16',min:1,max:60000},
 wifi_enabled:{type:'bool'},wifi_sta_enabled:{type:'bool'},wifi_ap_enabled:{type:'bool'},wifi_persist_credentials:{type:'bool'},
 wifi_sta_ssid:{type:'str',maxBytes:31},wifi_sta_password:{type:'str',maxBytes:63},
 wifi_ap_ssid:{type:'str',maxBytes:31},wifi_ap_password:{type:'str',maxBytes:63},
 wifi_sta_channel:{type:'u8',min:0,max:14},wifi_ap_channel:{type:'u8',min:1,max:14},
});
export function validateC6Configuration(document){
 const records=document.entries().filter(record=>record.key.startsWith('c6_companion.'));
 if(!records.length)return;
 const byKey=new Map(records.map(record=>[record.key,record]));
 for(const [name,spec] of Object.entries(C6_CONFIG_FIELDS)){
  const field='c6_companion.'+name,record=byKey.get(field),value=record?.value;
  const fail=()=>{throw Object.assign(Error('C6_CONFIG_INVALID'),{code:'C6_CONFIG_INVALID',field});};
  if(!record||record.type!==spec.type)fail();
  if(spec.type==='bool'&&typeof value!=='boolean')fail();
  if(spec.min!==undefined&&(!Number.isSafeInteger(value)||value<spec.min||value>spec.max))fail();
  if(spec.type==='str'&&(typeof value!=='string'||value.includes('\0')||new TextEncoder().encode(value).length>spec.maxBytes))fail();
 }
 const shared=document.entries().find(record=>record.key==='wifi.enabled');
 const enabled=byKey.get('c6_companion.wifi_enabled').value||(shared?.type==='bool'&&shared.value===true&&!!byKey.get('c6_companion.wifi_sta_ssid').value);
 if(enabled)for(const mode of ['sta','ap']){
  if(byKey.get(`c6_companion.wifi_${mode}_enabled`).value&&!byKey.get(`c6_companion.wifi_${mode}_ssid`).value)
   throw Object.assign(Error('C6_CONFIG_SSID_REQUIRED'),{code:'C6_CONFIG_SSID_REQUIRED',field:`c6_companion.wifi_${mode}_ssid`});
 }
 const password=byKey.get('c6_companion.wifi_ap_password').value;
 if(enabled&&byKey.get('c6_companion.wifi_ap_enabled').value&&password&&new TextEncoder().encode(password).length<8)
  throw Object.assign(Error('C6_CONFIG_AP_PASSWORD'),{code:'C6_CONFIG_AP_PASSWORD',field:'c6_companion.wifi_ap_password'});
}
