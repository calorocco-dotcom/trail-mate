export const geocachingConnectionKeys=['disconnectedEdits','disconnectedVerify','noDevice','authorVerified'];
export const geocachingConnectionLabels={
 en:['USB disconnected. Unsaved edits remain in this page.','USB disconnected; reconnect to verify the author.','No device selected.','Device author verified. Directory status is independent.'],
 'zh-Hans':['USB 已断开，未保存编辑保留在本页面。','USB 已断开，重新连接后验证作者。','尚未选择设备。','设备作者已验证。目录连接状态独立显示。'],
 'zh-Hant-TW':['USB 已中斷，未儲存的編輯保留在本頁。','USB 已中斷，重新連接後驗證作者。','尚未選擇裝置。','裝置作者已驗證。目錄連線狀態獨立顯示。'],
 ja:['USB が切断されました。未保存の編集はこのページに残ります。','USB が切断されました。再接続して作成者を検証してください。','デバイスが選択されていません。','デバイスの作成者を検証しました。ディレクトリの接続状態は別に表示されます。'],
 ko:['USB가 연결 해제되었습니다. 저장하지 않은 편집 내용은 이 페이지에 남아 있습니다.','USB가 연결 해제되었습니다. 다시 연결하여 작성자를 검증하세요.','선택한 기기가 없습니다.','기기 작성자가 검증되었습니다. 디렉터리 연결 상태는 별도로 표시됩니다.'],
 ar:['انقطع USB. تبقى التعديلات غير المحفوظة في هذه الصفحة.','انقطع USB؛ أعد الاتصال للتحقق من المؤلف.','لم يتم اختيار جهاز.','تم التحقق من مؤلف الجهاز. تُعرض حالة اتصال الدليل بشكل مستقل.'],
 ru:['USB отключён. Несохранённые изменения остаются на этой странице.','USB отключён; подключитесь снова для проверки автора.','Устройство не выбрано.','Автор устройства проверен. Состояние подключения каталога отображается отдельно.'],
 de:['USB getrennt. Ungespeicherte Änderungen bleiben auf dieser Seite.','USB getrennt; erneut verbinden, um den Autor zu prüfen.','Kein Gerät ausgewählt.','Geräteautor verifiziert. Der Verbindungsstatus des Verzeichnisses wird separat angezeigt.'],
 es:['USB desconectado. Los cambios sin guardar permanecen en esta página.','USB desconectado; vuelve a conectar para verificar al autor.','No se ha seleccionado un dispositivo.','Autor del dispositivo verificado. El estado del directorio se muestra por separado.'],
 fr:['USB déconnecté. Les modifications non enregistrées restent sur cette page.','USB déconnecté ; reconnectez pour vérifier l’auteur.','Aucun appareil sélectionné.','Auteur de l’appareil vérifié. L’état de connexion du répertoire est affiché séparément.'],
 it:['USB scollegato. Le modifiche non salvate restano in questa pagina.','USB scollegato; ricollega per verificare l’autore.','Nessun dispositivo selezionato.','Autore del dispositivo verificato. Lo stato di connessione della directory viene mostrato separatamente.'],
};
export function geocachingConnectionText(locale){const values=geocachingConnectionLabels[locale];if(!values||values.length!==geocachingConnectionKeys.length)throw Error(`Incomplete geocaching connection catalog: ${locale}`);return Object.fromEntries(geocachingConnectionKeys.map((key,index)=>[key,values[index]]));}
