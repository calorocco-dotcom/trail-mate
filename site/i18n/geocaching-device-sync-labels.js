const labels={
 en:['Sync to device','Publication and directory proof are archived on the device.','Device synchronization is unconfirmed. Retry with the same publication.'],
 'zh-Hans':['同步到设备','发布内容与目录签名证明已归档到设备。','设备同步尚未确认，请使用同一发布记录重试。'],
 'zh-Hant-TW':['同步至裝置','發布內容與目錄簽章證明已封存至裝置。','裝置同步尚未確認，請使用同一發布記錄重試。'],
 ja:['デバイスに同期','公開内容と署名付き証明をデバイスに保存しました。','同期は未確認です。同じ公開記録で再試行してください。'],
 ko:['기기에 동기화','게시 내용과 서명된 증명이 기기에 보관되었습니다.','동기화가 확인되지 않았습니다. 같은 게시 기록으로 다시 시도하세요.'],
 ar:['مزامنة مع الجهاز','تمت أرشفة المحتوى وإثبات الدليل الموقّع على الجهاز.','لم يتم تأكيد المزامنة. أعد المحاولة باستخدام سجل النشر نفسه.'],
 ru:['Синхронизировать с устройством','Публикация и подписанное подтверждение сохранены на устройстве.','Синхронизация не подтверждена. Повторите с той же публикацией.'],
 de:['Mit Gerät synchronisieren','Veröffentlichung und signierter Nachweis sind auf dem Gerät archiviert.','Synchronisierung unbestätigt. Mit derselben Veröffentlichung erneut versuchen.'],
 es:['Sincronizar con el dispositivo','La publicación y la prueba firmada están archivadas en el dispositivo.','Sincronización sin confirmar. Reintente con la misma publicación.'],
 fr:['Synchroniser avec l’appareil','La publication et la preuve signée sont archivées sur l’appareil.','Synchronisation non confirmée. Réessayez avec la même publication.'],
 it:['Sincronizza con il dispositivo','La pubblicazione e la prova firmata sono archiviate sul dispositivo.','Sincronizzazione non confermata. Riprova con la stessa pubblicazione.'],
};
export function geocachingDeviceSyncText(locale){const row=labels[locale];if(!row||row.length!==3)throw Error('Incomplete device synchronization translations');return {sync:row[0],confirmed:row[1],pending:row[2]};}
