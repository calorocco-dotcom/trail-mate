const labels={
 en:['Browser publication history','Open retained publication','Next page'],
 'zh-Hans':['浏览器发布历史','打开保留的发布记录','下一页'],
 'zh-Hant-TW':['瀏覽器發布歷史','開啟保留的發布記錄','下一頁'],
 ja:['ブラウザーの公開履歴','保存した公開記録を開く','次のページ'],
 ko:['브라우저 게시 기록','보관된 게시 기록 열기','다음 페이지'],
 ar:['سجل النشر في المتصفح','فتح سجل النشر المحفوظ','الصفحة التالية'],
 ru:['История публикаций в браузере','Открыть сохранённую публикацию','Следующая страница'],
 de:['Veröffentlichungsverlauf im Browser','Gespeicherte Veröffentlichung öffnen','Nächste Seite'],
 es:['Historial de publicaciones del navegador','Abrir publicación guardada','Página siguiente'],
 fr:['Historique des publications du navigateur','Ouvrir la publication conservée','Page suivante'],
 it:['Cronologia delle pubblicazioni nel browser','Apri pubblicazione conservata','Pagina successiva'],
};
export function geocachingHistoryText(locale){const row=labels[locale];if(!row||row.length!==3)throw Error('Incomplete publication history translations');return {title:row[0],open:row[1],next:row[2],refresh:row[0]};}
