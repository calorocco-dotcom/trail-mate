// Web management uses USB and the device author identity. Companion-app BLE
// settings are outside its supported scope; retain them in the source snapshot.
export function isWebConfigurationField(key){return typeof key==='string'&&!/(?:^|\.)(?:ble|bluetooth|mt_ble|mc_ble)(?:[._]|$)/.test(key);}
