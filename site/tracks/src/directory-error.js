export const TRACK_STORAGE_UNAVAILABLE='TRACK_DIRECTORY_STORAGE_UNAVAILABLE';

// Only a correlated, authenticated directory response may supply this status.
// Transport timeouts, other services and arbitrary 503 bodies stay distinct.
export function trackDirectoryError(error){
 if(error?.code===TRACK_STORAGE_UNAVAILABLE)return error;
 if(error?.status===503&&error?.reason==='storage unavailable')return Object.assign(Error('Track directory storage unavailable'),{code:TRACK_STORAGE_UNAVAILABLE,status:503,reason:'storage unavailable',cause:error});
 return error;
}
