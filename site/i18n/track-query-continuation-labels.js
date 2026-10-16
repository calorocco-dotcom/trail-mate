const keys=['next','pending','emptyPending','finished','changed'];
const copy={
 en:['Next batch of results','More results remain. Continue in the same area, or narrow the search.','No matches in this batch. Continue to search the remaining records.','The query for this area is complete.','The area, filters or directory changed. Start a new search.'],
 'zh-Hans':['下一批结果','还有后续结果，可以在同一范围继续查询，或缩小搜索条件。','这一批没有匹配项，可以继续查询剩余记录。','此范围的查询已完成。','范围、筛选或目录已经变化，请重新搜索。'],
 'zh-Hant-TW':['下一批結果','還有後續結果，可在相同範圍繼續查詢，或縮小搜尋條件。','這批沒有符合項目，可繼續查詢剩餘記錄。','此範圍的查詢已完成。','範圍、篩選或目錄已變更，請重新搜尋。'],
 ja:['次の結果','続きがあります。同じ範囲で続けるか、条件を絞ってください。','この回には一致がありません。残りのレコードを検索できます。','この範囲の検索は完了しました。','範囲、条件、ディレクトリが変わりました。検索し直してください。'],
 ko:['다음 결과','결과가 더 있습니다. 같은 범위에서 계속 검색하거나 조건을 좁히세요.','이번에는 일치하는 결과가 없습니다. 나머지 기록을 계속 검색할 수 있습니다.','이 범위의 검색이 완료되었습니다.','범위, 필터 또는 디렉터리가 변경되었습니다. 새로 검색하세요.'],
 ar:['الدفعة التالية من النتائج','توجد نتائج أخرى. تابع في المنطقة نفسها أو ضيّق البحث.','لا توجد مطابقات في هذه الدفعة. تابع البحث في السجلات المتبقية.','اكتمل البحث في هذه المنطقة.','تغيّرت المنطقة أو المرشحات أو الدليل. ابدأ بحثًا جديدًا.'],
 ru:['Следующая порция результатов','Есть продолжение. Продолжите в той же области или уточните условия.','В этой порции совпадений нет. Можно продолжить поиск оставшихся записей.','Поиск в этой области завершён.','Область, фильтры или каталог изменились. Начните новый поиск.'],
 de:['Nächste Ergebnisse','Weitere Ergebnisse verfügbar. Im selben Gebiet fortfahren oder Suche eingrenzen.','Keine Treffer in dieser Abfrage. Die übrigen Einträge können weiter durchsucht werden.','Die Suche in diesem Gebiet ist abgeschlossen.','Gebiet, Filter oder Verzeichnis geändert. Starten Sie eine neue Suche.'],
 es:['Siguiente lote de resultados','Hay más resultados. Continúa en la misma zona o acota la búsqueda.','No hay coincidencias en este lote. Puedes seguir buscando en los registros restantes.','La consulta de esta zona ha terminado.','La zona, los filtros o el directorio han cambiado. Inicia otra búsqueda.'],
 fr:['Résultats suivants','Il reste des résultats. Continuez dans la même zone ou affinez les critères.','Aucun résultat dans ce lot. Vous pouvez poursuivre la recherche dans les entrées restantes.','La recherche dans cette zone est terminée.','La zone, les filtres ou le répertoire ont changé. Relancez la recherche.'],
 it:['Risultati successivi','Ci sono altri risultati. Continua nella stessa area o restringi la ricerca.','Nessuna corrispondenza in questo gruppo. Puoi cercare nelle registrazioni rimanenti.','La ricerca in questa area è completata.','Area, filtri o directory sono cambiati. Avvia una nuova ricerca.']
};
export function trackQueryContinuationText(locale){const values=copy[locale];if(values?.length!==keys.length)throw Error('Incomplete track continuation locale: '+locale);return Object.fromEntries(keys.map((key,index)=>[key,values[index]]));}
