const encoder=new TextEncoder();
// Mirrors the bounded C++ sharing profile. Validation never normalizes dates
// or assigns a timezone. Raw XML/source bytes remain unchanged.
export function validTrackDateTime(input,{allowUnknown=false}={}){
 if(typeof input!=='string')return false;
 const text=input.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g,'');if(!text)return allowUnknown;if(encoder.encode(text).length>63)return false;
 const match=/^(-?)([0-9]{4,})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\.([0-9]+))?(Z|[+-][0-9]{2}:[0-9]{2})?$/.exec(text);
 if(!match)return false;
 const [,sign,yearText,monthText,dayText,hourText,minuteText,secondText,fraction='',zone='']=match;
 const year=BigInt(yearText);if(!year||yearText.length>4&&yearText[0]==='0')return false;
 const calendarYear=sign==='-'?1n-year:year,leap=calendarYear%4n===0n&&(calendarYear%100n!==0n||calendarYear%400n===0n);
 const month=Number(monthText),day=Number(dayText),hour=Number(hourText),minute=Number(minuteText),second=Number(secondText);
 if(month<1||month>12||hour>24||minute>59||second>59)return false;
 const days=month===2?(leap?29:28):[4,6,9,11].includes(month)?30:31;
 if(day<1||day>days||hour===24&&(minute||second||/[1-9]/.test(fraction)))return false;
 if(zone&&zone!=='Z'){const zoneHour=Number(zone.slice(1,3)),zoneMinute=Number(zone.slice(4));if(zoneHour>14||zoneMinute>59||zoneHour===14&&zoneMinute)return false;}
 return true;
}
