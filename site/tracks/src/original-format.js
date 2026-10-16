// Local originals are never inferred from the converted KML or sent as resources.
export const ORIGINAL_FORMATS=Object.freeze({
 kml:Object.freeze({extension:'kml',mime:'application/vnd.google-earth.kml+xml'}),
 gpx:Object.freeze({extension:'gpx',mime:'application/gpx+xml'}),
 csv:Object.freeze({extension:'csv',mime:'text/csv'}),
 bin:Object.freeze({extension:'bin',mime:'application/octet-stream'}),
});
export function originalFormat(value='kml'){
 if(typeof value!=='string'||!Object.hasOwn(ORIGINAL_FORMATS,value))throw Error('Invalid original format');
 return ORIGINAL_FORMATS[value];
}
export function originalFilenameMatches(filename,format='kml'){
 const spec=originalFormat(format);
 return format==='kml'||typeof filename==='string'&&filename.toLowerCase().endsWith('.'+spec.extension);
}
