const keys=['search','changedOnly','results','empty','clear','more','edited'];
const labels={
 en:['Search all settings by name or description','Only modified settings','{count} matching settings','No matching settings','Clear search','Show more','Modified'],
 'zh-Hans':['按名称或说明搜索全部设置','只看已修改设置','找到 {count} 项设置','没有匹配的设置','清除搜索','显示更多','已修改'],
 'zh-Hant-TW':['依名稱或說明搜尋所有設定','只看已修改設定','找到 {count} 項設定','沒有符合的設定','清除搜尋','顯示更多','已修改'],
 ja:['名前や説明で全設定を検索','変更した設定のみ','一致する設定 {count} 件','一致する設定はありません','検索をクリア','さらに表示','変更済み'],
 ko:['이름 또는 설명으로 모든 설정 검색','수정한 설정만','일치하는 설정 {count}개','일치하는 설정 없음','검색 지우기','더 보기','수정됨'],
 ar:['البحث في جميع الإعدادات بالاسم أو الوصف','الإعدادات المعدلة فقط','{count} إعدادات مطابقة','لا توجد إعدادات مطابقة','مسح البحث','عرض المزيد','معدّل'],
 ru:['Поиск всех настроек по названию или описанию','Только изменённые настройки','Найдено настроек: {count}','Подходящих настроек нет','Очистить поиск','Показать ещё','Изменено'],
 de:['Alle Einstellungen nach Name oder Beschreibung suchen','Nur geänderte Einstellungen','{count} passende Einstellungen','Keine passenden Einstellungen','Suche löschen','Mehr anzeigen','Geändert'],
 es:['Buscar ajustes por nombre o descripción','Solo ajustes modificados','{count} ajustes encontrados','No hay ajustes coincidentes','Borrar búsqueda','Mostrar más','Modificado'],
 fr:['Rechercher les réglages par nom ou description','Réglages modifiés uniquement','{count} réglages correspondants','Aucun réglage correspondant','Effacer la recherche','Afficher plus','Modifié'],
 it:['Cerca tutte le impostazioni per nome o descrizione','Solo impostazioni modificate','{count} impostazioni trovate','Nessuna impostazione corrispondente','Cancella ricerca','Mostra altro','Modificata'],
};
export function configFinderText(locale){const values=labels[locale];if(!values||values.length!==keys.length)throw Error('Incomplete setting search translations');return Object.fromEntries(keys.map((key,index)=>[key,values[index]]));}
