// Remains external to management bundles: every consumer shares runtime.js.
import {locales} from './registry.js';
import {getLocale,setLocale,subscribeLocale,translate} from './runtime.js';
import {managementText} from './management-labels.js';

const pageNames={
 en:['Device management','Track workspace'],
 'zh-Hans':['设备管理','轨迹工作台'],
 'zh-Hant-TW':['裝置管理','軌跡工作區'],
 ja:['デバイス管理','軌跡ワークスペース'],
 ko:['기기 관리','트랙 작업 공간'],
 ar:['إدارة الجهاز','مساحة عمل المسارات'],
 ru:['Управление устройством','Рабочая область треков'],
 de:['Geräteverwaltung','Track-Arbeitsbereich'],
 es:['Gestión del dispositivo','Espacio de trabajo de recorridos'],
 fr:['Gestion de l’appareil','Espace de travail des parcours'],
 it:['Gestione del dispositivo','Area di lavoro dei percorsi'],
};
let initialization;
export function initializeManagementLanguage(){
 initialization??=setLocale(new URL(globalThis.location.href).searchParams.get('lang')??'en');
 return initialization;
}
export function managementLanguageSnapshot(){
 const locale=getLocale();
 return {locale,locales,text:managementText(locale),languageLabel:translate('common.language'),deviceTitle:pageNames[locale][0],tracksTitle:pageNames[locale][1]};
}
export async function watchManagementLanguage(listener){
 await initializeManagementLanguage();
 listener(managementLanguageSnapshot());
 return subscribeLocale(()=>listener(managementLanguageSnapshot()));
}
export async function changeManagementLanguage(locale){
 await setLocale(locale);
 // setLocale is last-request-wins. A late result must never restore an old URL.
 const url=new URL(globalThis.location.href);
 url.searchParams.set('lang',getLocale());
 globalThis.history.replaceState(globalThis.history.state,'',url.href);
}
export function managementLanguageHref(href){
 const url=new URL(href,globalThis.location.href);
 if(url.origin===globalThis.location.origin)url.searchParams.set('lang',getLocale());
 return url.href;
}
