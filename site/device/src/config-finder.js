const normalize=value=>String(value??'').normalize('NFKC').toLocaleLowerCase();
import {isWebConfigurationField} from './config-web-scope.js';
// Values are deliberately excluded: searching never indexes passwords or keys.
export function findConfigurationFields(rows,query,{changedOnly=false,changes=new Set()}={}){
 const tokens=normalize(query).trim().split(/\s+/u).filter(Boolean);
 return rows.filter(row=>isWebConfigurationField(row.key)&&(!changedOnly||changes.has(row.key))&&tokens.every(token=>normalize([row.label,row.help,row.key,row.groupLabel].join(' ')).includes(token)));
}
