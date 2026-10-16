// Normalize only a field the user edited. Untouched KML number spellings are
// never rewritten just because the display language changed.
export function overlayNumberInput(raw,locale){
 const decimal=new Intl.NumberFormat(locale).formatToParts(1.1).find(part=>part.type==='decimal').value;
 // ICU may use Latin decimal punctuation for ar; explicitly typed Arabic
 // decimals still belong with the native digit shapes normalized above.
 let result=String(raw).replace(/[٠-٩۰-۹０-９]/g,char=>{const n=char.charCodeAt(0);return String(n>=0xff10?n-0xff10:n>=0x6f0?n-0x6f0:n-0x660);}).replace(/−/g,'-').replace(/٫/g,'.').trim();
 if(decimal!=='.')result=result.replaceAll(decimal,'.');return result;
}
