export function configurationMaskOptions(key){return key==='gps.sat_mask'?[{value:1,label:'GPS'},{value:2,label:'GLONASS'},{value:4,label:'Galileo'},{value:8,label:'BeiDou'}]:null;}
export function toggleConfigurationMask(value,bit,enabled){
 if(!Number.isInteger(value)||value<0||value>255||![1,2,4,8].includes(bit)||typeof enabled!=='boolean')throw Error('Invalid satellite mask selection');
 return enabled?value|bit:value&~bit;
}
