const labels={
 'zh-Hans':['恢复到当前设备','更换设备或恢复同一身份后，可用已确认的发布记录恢复轨迹维护。不会重新发布，也不会替换设备上的不同版本。'],
 'zh-Hant-TW':['還原至目前裝置','更換裝置或還原相同身分後，可用已確認的發布紀錄還原軌跡維護。不會重新發布，也不會取代裝置上的不同版本。'],
 en:['Restore to current device','After replacing your device or restoring the same identity, recover track maintenance from confirmed publication history. This does not republish or replace a different device version.'],
 ja:['現在のデバイスに復元','デバイスの交換や同じIDの復元後、確認済みの公開履歴から軌跡の管理を復元できます。再公開や異なる版の上書きは行いません。'],
 ko:['현재 기기로 복원','기기를 교체하거나 같은 신원을 복원한 뒤 확인된 게시 기록으로 트랙 관리를 복원할 수 있습니다. 다시 게시하거나 다른 버전을 덮어쓰지 않습니다.'],
 ar:['الاستعادة إلى الجهاز الحالي','بعد استبدال الجهاز أو استعادة الهوية نفسها، استعد إدارة المسار من سجل النشر المؤكد. لا يعاد النشر ولا يستبدل إصدار مختلف على الجهاز.'],
 ru:['Восстановить на текущем устройстве','После замены устройства или восстановления той же личности восстановите управление маршрутом из подтверждённой истории публикации. Повторной публикации и замены другой версии не будет.'],
 de:['Auf aktuellem Gerät wiederherstellen','Nach Gerätewechsel oder Wiederherstellung derselben Identität lässt sich die Track-Verwaltung aus bestätigten Veröffentlichungen wiederherstellen. Keine erneute Veröffentlichung oder Ersetzung einer anderen Version.'],
 es:['Restaurar en el dispositivo actual','Tras cambiar de dispositivo o restaurar la misma identidad, recupere la gestión de rutas desde el historial de publicación confirmado. No se vuelve a publicar ni se reemplaza otra versión.'],
 fr:['Restaurer sur l’appareil actuel','Après un changement d’appareil ou la restauration de la même identité, récupérez la gestion de la trace depuis l’historique confirmé. Aucune nouvelle publication ni remplacement d’une autre version.'],
 it:['Ripristina sul dispositivo attuale','Dopo aver cambiato dispositivo o ripristinato la stessa identità, recupera la gestione della traccia dalla cronologia confermata. Non viene ripubblicata né sostituita una versione diversa.'],
};
export function trackDeviceRestoreText(locale){const row=labels[locale];if(!row||row.length!==2)throw Error('Incomplete track restoration translations');return{restore:row[0],intro:row[1]};}
