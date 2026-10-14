const keys=['title','previous','next','page','empty'];
const labels={
 en:['KML contents','Previous page','Next page','Page {page} / {pages}','No geographic features'],
 'zh-Hans':['KML 内容','上一页','下一页','第 {page} / {pages} 页','没有地理要素'],
 'zh-Hant-TW':['KML 內容','上一頁','下一頁','第 {page} / {pages} 頁','沒有地理要素'],
 ja:['KML の内容','前のページ','次のページ','{page} / {pages} ページ','地理要素がありません'],
 ko:['KML 내용','이전 페이지','다음 페이지','{page} / {pages} 페이지','지리 요소가 없습니다'],
 ar:['محتويات KML','الصفحة السابقة','الصفحة التالية','الصفحة {page} / {pages}','لا توجد معالم جغرافية'],
 ru:['Содержимое KML','Предыдущая страница','Следующая страница','Страница {page} / {pages}','Нет географических объектов'],
 de:['KML-Inhalt','Vorherige Seite','Nächste Seite','Seite {page} / {pages}','Keine geografischen Elemente'],
 es:['Contenido KML','Página anterior','Página siguiente','Página {page} / {pages}','No hay elementos geográficos'],
 fr:['Contenu KML','Page précédente','Page suivante','Page {page} / {pages}','Aucun élément géographique'],
 it:['Contenuto KML','Pagina precedente','Pagina successiva','Pagina {page} / {pages}','Nessun elemento geografico'],
};
export function kmlContentText(locale){const values=labels[locale];if(!values||values.length!==keys.length)throw Error(`Missing KML content locale: ${locale}`);return Object.fromEntries(keys.map((key,index)=>[key,values[index]]));}
