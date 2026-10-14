const keys=['kml','index','resource','publishing','paused','pause'];
const labels={
 en:['Uploading KML','Uploading resource index','Uploading attachment','Waiting for directory confirmation','Transfer paused. The original task and signature are retained; resume to continue.','Pause transfer'],
 'zh-Hans':['正在上传 KML','正在上传资源索引','正在上传附件','正在等待目录确认','传输已暂停。原任务和签名已保留，可继续原任务。','暂停传输'],
 'zh-Hant-TW':['正在上傳 KML','正在上傳資源索引','正在上傳附件','正在等待目錄確認','傳輸已暫停。原任務與簽名已保留，可繼續原任務。','暫停傳輸'],
 ja:['KMLを送信中','リソース索引を送信中','添付を送信中','ディレクトリの確認待ち','転送を一時停止しました。元の処理と署名を保持し、再開できます。','転送を一時停止'],
 ko:['KML 업로드 중','리소스 색인 업로드 중','첨부 파일 업로드 중','디렉터리 확인 대기 중','전송이 일시 중지되었습니다. 원래 작업과 서명을 보존했으며 계속할 수 있습니다.','전송 일시 중지'],
 ar:['جارٍ رفع KML','جارٍ رفع فهرس الموارد','جارٍ رفع المرفق','بانتظار تأكيد الدليل','أُوقف النقل مؤقتًا. احتُفظ بالمهمة والتوقيع الأصليين؛ يمكن المتابعة.','إيقاف النقل مؤقتًا'],
 ru:['Загрузка KML','Загрузка индекса ресурсов','Загрузка вложения','Ожидание подтверждения каталога','Передача приостановлена. Исходная задача и подпись сохранены; можно продолжить.','Приостановить передачу'],
 de:['KML wird hochgeladen','Ressourcenindex wird hochgeladen','Anhang wird hochgeladen','Warten auf Verzeichnisbestätigung','Übertragung pausiert. Ursprüngliche Aufgabe und Signatur bleiben erhalten; du kannst fortsetzen.','Übertragung pausieren'],
 es:['Subiendo KML','Subiendo índice de recursos','Subiendo adjunto','Esperando confirmación del directorio','Transferencia pausada. Se conservan la tarea y firma originales; puede continuar.','Pausar transferencia'],
 fr:['Envoi du KML','Envoi de l’index des ressources','Envoi de la pièce jointe','En attente de confirmation du répertoire','Transfert suspendu. La tâche et la signature d’origine sont conservées ; vous pouvez reprendre.','Suspendre le transfert'],
 it:['Caricamento KML','Caricamento indice risorse','Caricamento allegato','In attesa di conferma della directory','Trasferimento sospeso. Attività e firma originali sono conservate; puoi riprendere.','Sospendi trasferimento'],
};
export function trackTransferText(locale){const row=labels[locale];if(!row||row.length!==keys.length)throw Error('Incomplete transfer translations');return Object.fromEntries(keys.map((key,index)=>[key,row[index]]));}
