const labels={
 en:['Save directory proof','Directory proof is saved in this browser. Device synchronization is pending.'],
 'zh-Hans':['保存目录签名证明','目录签名证明已保存在此浏览器，设备同步尚未完成。'],
 'zh-Hant-TW':['儲存目錄簽章證明','目錄簽章證明已儲存在此瀏覽器，裝置同步尚未完成。'],
 ja:['ディレクトリの証明を保存','署名付き証明はこのブラウザーに保存されています。デバイスへの同期は未完了です。'],
 ko:['디렉터리 증명 저장','서명된 증명이 이 브라우저에 저장되었습니다. 기기 동기화는 아직 완료되지 않았습니다.'],
 ar:['حفظ إثبات الدليل','تم حفظ الإثبات الموقّع في هذا المتصفح. مزامنة الجهاز لم تكتمل بعد.'],
 ru:['Сохранить подтверждение каталога','Подписанное подтверждение сохранено в этом браузере. Синхронизация с устройством ещё не завершена.'],
 de:['Verzeichnisnachweis speichern','Der signierte Nachweis ist in diesem Browser gespeichert. Die Synchronisierung mit dem Gerät steht noch aus.'],
 es:['Guardar prueba del directorio','La prueba firmada está guardada en este navegador. La sincronización con el dispositivo está pendiente.'],
 fr:['Enregistrer la preuve du répertoire','La preuve signée est enregistrée dans ce navigateur. La synchronisation avec l’appareil reste à effectuer.'],
 it:['Salva la prova della directory','La prova firmata è salvata in questo browser. La sincronizzazione con il dispositivo è ancora da completare.'],
};
const upgrade={en:'This directory version cannot complete web publication and device synchronization. Select another directory or update this service.','zh-Hans':'此目录版本暂不支持完成网页发布和设备同步。请选择其他目录，或更新此目录服务。','zh-Hant-TW':'此目錄版本暫不支援完成網頁發布與裝置同步。請選擇其他目錄，或更新此目錄服務。',ja:'このディレクトリのバージョンでは Web 公開とデバイス同期を完了できません。別のディレクトリを選ぶか、サービスを更新してください。',ko:'이 디렉터리 버전으로는 웹 게시와 기기 동기화를 완료할 수 없습니다. 다른 디렉터리를 선택하거나 서비스를 업데이트하세요.',ar:'لا يمكن لهذا الإصدار من الدليل إكمال النشر عبر الويب ومزامنة الجهاز. اختر دليلاً آخر أو حدّث الخدمة.',ru:'Эта версия каталога не позволяет завершить веб-публикацию и синхронизацию устройства. Выберите другой каталог или обновите сервис.',de:'Diese Verzeichnisversion kann Web-Veröffentlichung und Gerätesynchronisierung nicht abschließen. Anderes Verzeichnis wählen oder den Dienst aktualisieren.',es:'Esta versión del directorio no permite completar la publicación web y la sincronización. Seleccione otro directorio o actualice el servicio.',fr:'Cette version du répertoire ne permet pas de terminer la publication web et la synchronisation. Choisissez un autre répertoire ou mettez le service à jour.',it:'Questa versione della directory non consente di completare pubblicazione web e sincronizzazione. Scegli un’altra directory o aggiorna il servizio.'};
export function geocachingReceiptText(locale){const values=labels[locale];if(!values||values.length!==2||!upgrade[locale])throw Error('Incomplete receipt translations');return {save:values[0],retained:values[1],upgrade:upgrade[locale]};}
