// These two rules come from repairLegacyMqttConfig in the firmware bridge.
// Describe normalization without replacing the user's staged values.
export function moduleNormalizationRule(path){
 if(!Array.isArray(path)||path[0]!=='mqtt')return undefined;
 if(path.length===1||(path.length===2&&path[1]==='map_report_settings'))return 'restoredPresence';
 if(path.length===2&&['address','username','password','root'].includes(path[1]))return 'blankMqttValue';
 return undefined;
}
