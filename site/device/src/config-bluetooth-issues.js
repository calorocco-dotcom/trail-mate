export function configBluetoothIssues(document){
 let mode,pin;try{mode=document.get('mt_ble.mode');pin=document.get('mt_ble.fixed_pin');}catch{return [];}
 if(mode?.type==='u8'&&mode.value===2&&pin?.type==='u32'&&pin.value!==0)return [{key:'mt_ble.fixed_pin',code:'BLE_NO_PIN_REQUIRES_ZERO',params:{},message:'No PIN mode requires fixed PIN zero'}];
 return [];
}
