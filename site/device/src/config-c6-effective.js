export function sharedWifiOverridesC6(document){
 if(!document)return false;
 const records=new Map(document.entries().map(row=>[row.key,row]));
 const flag=key=>records.get(key)?.type==='bool'&&records.get(key).value===true;
 const ssid=records.get('c6_companion.wifi_sta_ssid');
 return flag('wifi.enabled')&&ssid?.type==='str'&&typeof ssid.value==='string'&&ssid.value.length>0&&
  (!flag('c6_companion.wifi_enabled')||!flag('c6_companion.wifi_sta_enabled'));
}
