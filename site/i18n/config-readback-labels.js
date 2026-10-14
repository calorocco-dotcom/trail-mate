const keys=['title','pending','matched','mismatch','wrongDevice','unverified','differences','before','after'];
const copy={
 en:['Saved configuration readback','Commit received. Reconnect or read the device to compare the saved configuration.','The same verified author returned configuration matching this save.','Device readback differs from the saved configuration. Review the differences below.','A different device author is connected. This save cannot be verified against its configuration.','Verify the same device author and read again to check this save.','Readback differences','Submitted configuration','Device readback'],
 'zh-Hans':['保存后的配置核对','已收到提交回执。重新连接或读取设备后，对照本次保存的配置。','同一已验证作者的设备读回配置与本次保存一致。','设备读回配置与本次保存存在差异，请查看下方对照。','当前连接的是另一位设备作者，不能用它的配置确认本次保存。','请验证同一设备作者，再次读取以核对本次保存。','设备读回差异','提交的配置','设备读回'],
 'zh-Hant-TW':['儲存後的設定核對','已收到提交回執。重新連接或讀取裝置後，對照本次儲存的設定。','同一已驗證作者的裝置讀回設定與本次儲存一致。','裝置讀回設定與本次儲存有差異，請查看下方對照。','目前連接的是另一位裝置作者，不能用其設定確認本次儲存。','請驗證同一裝置作者，再次讀取以核對本次儲存。','裝置讀回差異','提交的設定','裝置讀回'],
 ja:['保存後の設定確認','コミット応答を受信しました。再接続または再読み込みして保存した設定と比較してください。','同じ検証済み作成者のデバイス設定が今回の保存と一致しました。','読み込んだ設定が今回の保存と異なります。下の差分を確認してください。','別のデバイス作成者が接続されています。この保存の確認には使えません。','同じデバイス作成者を検証してから再読み込みしてください。','読み込みの差分','送信した設定','デバイスの設定'],
 ko:['저장 후 설정 확인','커밋 응답을 받았습니다. 다시 연결하거나 기기 설정을 읽어 이번 저장과 비교하세요.','동일한 검증된 작성자의 기기 설정이 이번 저장과 일치합니다.','기기에서 읽은 설정이 이번 저장과 다릅니다. 아래 차이를 확인하세요.','다른 기기 작성자가 연결되어 있어 이번 저장을 확인할 수 없습니다.','동일한 기기 작성자를 검증한 후 다시 읽어 저장을 확인하세요.','읽은 설정의 차이','제출한 설정','기기에서 읽은 설정'],
 ar:['التحقق من الإعدادات المحفوظة','وصل إيصال الاعتماد. أعد الاتصال أو اقرأ الجهاز لمقارنة الإعدادات المحفوظة.','أعاد الجهاز ذو هوية المؤلف الموثقة نفسها إعدادات مطابقة لهذا الحفظ.','تختلف إعدادات الجهاز المقروءة عن هذا الحفظ. راجع الفروق أدناه.','هوية مؤلف جهاز آخر متصلة. لا يمكن التحقق من هذا الحفظ باستخدام إعداداته.','تحقق من هوية مؤلف الجهاز نفسه ثم اقرأ مجددًا لمراجعة هذا الحفظ.','فروق القراءة','الإعدادات المرسلة','الإعدادات المقروءة من الجهاز'],
 ru:['Проверка сохранённых настроек','Получено подтверждение записи. Переподключитесь или прочитайте настройки для сравнения.','Устройство с тем же проверенным автором вернуло настройки, совпадающие с этим сохранением.','Прочитанные настройки отличаются от сохранённых. Проверьте различия ниже.','Подключено устройство другого автора. Его настройки не подтверждают это сохранение.','Проверьте того же автора устройства и прочитайте настройки заново.','Различия при чтении','Отправленные настройки','Настройки устройства'],
 de:['Gespeicherte Konfiguration prüfen','Übernahme bestätigt. Erneut verbinden oder lesen, um die gespeicherte Konfiguration zu vergleichen.','Das Gerät desselben verifizierten Autors liefert die zu diesem Speichern passende Konfiguration.','Die gelesene Konfiguration weicht vom gespeicherten Stand ab. Unterschiede unten prüfen.','Ein anderer Geräteautor ist verbunden. Dessen Konfiguration kann dieses Speichern nicht bestätigen.','Denselben Geräteautor verifizieren und erneut lesen, um dieses Speichern zu prüfen.','Unterschiede beim Rücklesen','Übermittelte Konfiguration','Gelesene Gerätekonfiguration'],
 es:['Comprobar la configuración guardada','Se recibió la confirmación. Reconecta o lee el dispositivo para comparar la configuración guardada.','El dispositivo del mismo autor verificado devuelve una configuración que coincide con este guardado.','La configuración leída difiere de la guardada. Revisa las diferencias abajo.','Está conectado otro autor de dispositivo. Su configuración no permite verificar este guardado.','Verifica el mismo autor del dispositivo y vuelve a leer para comprobar este guardado.','Diferencias de lectura','Configuración enviada','Configuración leída del dispositivo'],
 fr:['Vérifier la configuration enregistrée','Confirmation reçue. Reconnectez-vous ou relisez l’appareil pour comparer la configuration enregistrée.','L’appareil du même auteur vérifié renvoie une configuration conforme à cet enregistrement.','La configuration relue diffère de celle enregistrée. Examinez les différences ci-dessous.','Un autre auteur d’appareil est connecté. Sa configuration ne peut pas confirmer cet enregistrement.','Vérifiez le même auteur d’appareil puis relisez la configuration.','Différences de relecture','Configuration envoyée','Configuration relue'],
 it:['Verifica della configurazione salvata','Conferma ricevuta. Riconnetti o rileggi il dispositivo per confrontare la configurazione salvata.','Il dispositivo dello stesso autore verificato restituisce una configurazione corrispondente a questo salvataggio.','La configurazione riletta differisce da quella salvata. Controlla le differenze qui sotto.','È collegato un altro autore del dispositivo. La sua configurazione non può verificare questo salvataggio.','Verifica lo stesso autore del dispositivo e rileggi per controllare questo salvataggio.','Differenze di rilettura','Configurazione inviata','Configurazione riletta'],
};
const restored={
 en:['Configuration submitted for verification. Reconnect or read the device to compare it.','The device snapshot differs from the submitted snapshot. Field details were not retained after refresh. Review the device configuration or compare your backup.'],
 'zh-Hans':['配置等待读回核对。重新连接或读取设备后，对照本次提交。','设备快照与本次提交不同。刷新后未保留逐字段内容，请检查设备配置或对照配置备份。'],
 'zh-Hant-TW':['設定等待讀回核對。重新連接或讀取裝置後，對照本次提交。','裝置快照與本次提交不同。重新整理後未保留逐欄位內容，請檢查裝置設定或對照備份。'],
 ja:['設定の読み込み確認を待っています。再接続または再読み込みして今回の送信と比較してください。','デバイスのスナップショットが今回の送信と異なります。再読み込み後は各項目の内容を保持していません。設定またはバックアップを確認してください。'],
 ko:['설정 읽기 확인을 기다립니다. 다시 연결하거나 읽어 이번 제출과 비교하세요.','기기 스냅샷이 제출한 스냅샷과 다릅니다. 새로고침 후에는 필드별 내용을 보관하지 않습니다. 기기 설정이나 백업을 확인하세요.'],
 ar:['الإعدادات بانتظار التحقق بالقراءة. أعد الاتصال أو اقرأ الجهاز لمقارنتها بهذا الإرسال.','تختلف لقطة الجهاز عن اللقطة المرسلة. لم تُحفظ تفاصيل الحقول بعد تحديث الصفحة. راجع إعدادات الجهاز أو نسختك الاحتياطية.'],
 ru:['Настройки ожидают проверки чтением. Переподключитесь или прочитайте устройство для сравнения.','Снимок устройства отличается от отправленного. После обновления страницы значения полей не сохраняются. Проверьте настройки или резервную копию.'],
 de:['Konfiguration wartet auf Rückleseprüfung. Erneut verbinden oder lesen, um diesen Stand zu vergleichen.','Der Geräteschnappschuss weicht vom übermittelten Stand ab. Nach Neuladen bleiben keine Feldinhalte erhalten. Gerätekonfiguration oder Sicherung prüfen.'],
 es:['La configuración espera la comprobación de lectura. Reconecta o lee el dispositivo para compararla.','La instantánea del dispositivo difiere de la enviada. Los detalles de los campos no se conservaron tras recargar. Revisa la configuración o tu copia de seguridad.'],
 fr:['La configuration attend une vérification par relecture. Reconnectez-vous ou relisez l’appareil pour la comparer.','L’instantané de l’appareil diffère de celui envoyé. Les valeurs des champs ne sont pas conservées après rechargement. Vérifiez la configuration ou votre sauvegarde.'],
 it:['La configurazione attende la verifica tramite rilettura. Riconnetti o rileggi il dispositivo per confrontarla.','L’istantanea del dispositivo differisce da quella inviata. I dettagli dei campi non sono conservati dopo il ricaricamento. Controlla la configurazione o il backup.'],
};
const unavailable={
 en:'This browser could not retain the readback checkpoint. Keep this page open until verification finishes.',
 'zh-Hans':'浏览器未能保留读回核对记录，请保持此页面打开直到核对完成。',
 'zh-Hant-TW':'瀏覽器未能保留讀回核對記錄，請保持此頁面開啟直到核對完成。',
 ja:'ブラウザーに確認記録を保存できませんでした。確認が終わるまでこのページを開いたままにしてください。',
 ko:'브라우저가 읽기 확인 기록을 보관하지 못했습니다. 확인이 끝날 때까지 이 페이지를 열어 두세요.',
 ar:'تعذّر على المتصفح حفظ سجل التحقق بالقراءة. أبقِ هذه الصفحة مفتوحة حتى اكتمال التحقق.',
 ru:'Браузер не смог сохранить запись проверки. Не закрывайте страницу до завершения проверки.',
 de:'Der Browser konnte den Prüfstand nicht speichern. Diese Seite bis zum Ende der Prüfung geöffnet lassen.',
 es:'El navegador no pudo conservar el registro de comprobación. Mantén esta página abierta hasta terminar la verificación.',
 fr:'Le navigateur n’a pas pu conserver le point de vérification. Gardez cette page ouverte jusqu’à la fin de la vérification.',
 it:'Il browser non ha potuto conservare il punto di verifica. Mantieni aperta questa pagina fino al termine della verifica.',
};
export function configReadbackText(locale){
 const values=copy[locale];if(!values||values.length!==keys.length)throw Error(`Incomplete readback catalog: ${locale}`);
 if(!restored[locale]||restored[locale].length!==2)throw Error(`Incomplete restored readback catalog: ${locale}`);
 if(!unavailable[locale])throw Error(`Incomplete readback storage catalog: ${locale}`);
 return {...Object.fromEntries(keys.map((key,index)=>[key,values[index]])),pending:restored[locale][0],mismatchSummary:restored[locale][1],storageUnavailable:unavailable[locale]};
}
