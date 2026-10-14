import {readTrackJobHistory} from './draft-store.js';
import {encodeTrackJobBinding} from './job-binding.js';
export async function exportTrackJobHistory(key){
 const record=await readTrackJobHistory(key),operation=Array.from(record.binding.operationId,b=>b.toString(16).padStart(2,'0')).join('');
 return {filename:`trailmate-task-${operation}.tmjob`,bytes:encodeTrackJobBinding(record.binding)};
}
