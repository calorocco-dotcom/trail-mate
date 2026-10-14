import {bytes,integer,validText,encode,decode} from './protocol.js';

const maxGeneration=0xffffffffffffffffn;
function generation(value,{allowZero=false}={}){
  if(typeof value!=='bigint'&&!Number.isSafeInteger(value))throw Error('草稿代次无效');
  const result=BigInt(value);
  if(result<(allowZero?0n:1n)||result>maxGeneration)throw Error('草稿代次超出范围');
  return result;
}
const scalar=value=>value<=BigInt(Number.MAX_SAFE_INTEGER)?Number(value):value;
export function validateDraftFields(fields){
  if(!fields||typeof fields!=='object'||Array.isArray(fields))throw Object.assign(Error('草稿字段格式无效'),{code:'GEO_DRAFT_FIELDS'});
  const {state=0,latitudeE7=null,longitudeE7=null,name='',description='',hint='',difficultyX2=2,terrainX2=2,containerSize=0}=fields;
  if(!integer(state,0,2)||!integer(difficultyX2,2,10)||!integer(terrainX2,2,10)||!integer(containerSize,0,5))throw Object.assign(Error('草稿状态、难度、地形或容器尺寸无效'),{code:'GEO_DRAFT_OPTIONS'});
  if((latitudeE7===null)!==(longitudeE7===null))throw Object.assign(Error('纬度与经度必须同时填写或同时留空'),{code:'GEO_DRAFT_COORDINATE_PAIR'});
  if(latitudeE7!==null&&(!integer(latitudeE7,-900000000,900000000)||!integer(longitudeE7,-1800000000,1799999999)))throw Object.assign(Error('草稿坐标超出范围'),{code:'GEO_DRAFT_COORDINATE_RANGE'});
  if(!validText(name,96)||!validText(description,2048,true)||!validText(hint,512,true))throw Object.assign(Error('草稿文本超出 UTF-8 字节限制或包含非法字符'),{code:'GEO_DRAFT_TEXT'});
  return {state,latitudeE7,longitudeE7,name,description,hint,difficultyX2,terrainX2,containerSize};
}
function wireFields(fields){
  const f=validateDraftFields(fields);
  return [f.state,f.latitudeE7,f.longitudeE7,f.name,f.description,f.hint,f.difficultyX2,f.terrainX2,f.containerSize];
}

// Exactly the existing device DraftView format. The browser edits user fields;
// the storage owner restores immutable author and base-hash metadata itself.
export function encodeDraftEdit(id,fields,expectedGeneration){
  if(!bytes(id,16))throw Error('草稿 ID 必须为 16 字节');
  if(fields?.authorKey!=null||fields?.baseHash!=null)throw Error('作者与基版本由设备维护，不能写入编辑命令');
  const previous=generation(expectedGeneration,{allowZero:true});
  if(previous===maxGeneration)throw Error('草稿代次已耗尽');
  return encode([null,null,wireFields(fields),scalar(previous+1n)]);
}

export function decodeDeviceDraft(id,raw){
  if(!bytes(id,16))throw Error('草稿 ID 必须为 16 字节');
  const value=decode(raw,32768);
  if(!Array.isArray(value)||value.length!==4||
     (value[0]!==null&&!bytes(value[0],64))||(value[1]!==null&&!bytes(value[1],32))||
     !Array.isArray(value[2])||value[2].length!==9)throw Error('设备草稿格式无效');
  const [state,latitudeE7,longitudeE7,name,description,hint,difficultyX2,terrainX2,containerSize]=value[2];
  const fields=validateDraftFields({state,latitudeE7,longitudeE7,name,description,hint,difficultyX2,terrainX2,containerSize});
  return {id:id.slice(),authorKey:value[0]?.slice()??null,baseHash:value[1]?.slice()??null,
    generation:scalar(generation(value[3])),...fields};
}

export function validateDraftPublication(fields){
  const checked=validateDraftFields(fields);
  if(checked.latitudeE7===null)throw Error('发布前必须选择藏宝点坐标');
  if(!validText(checked.name,96,false,true))throw Error('发布前必须填写藏宝点名称');
  return checked;
}
