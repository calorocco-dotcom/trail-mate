const labels={
 en:'The original index includes optional files unavailable locally. Saving attachment changes will remove those unavailable entries from the new package. Continue?',
 'zh-Hans':'原索引包含尚未取得的可选附件。保存附件修改会从新包中移除这些尚不可用的条目。是否继续？',
 'zh-Hant-TW':'原始索引包含尚未取得的選用附件。儲存附件修改會從新套件移除這些無法取得的項目。是否繼續？',
 ja:'元の索引に未取得の任意添付があります。添付の変更を保存すると、新しいパッケージから未取得の項目を除きます。続行しますか？',
 ko:'원본 인덱스에 받지 못한 선택 첨부가 있습니다. 첨부 변경을 저장하면 새 패키지에서 이 항목들이 제거됩니다. 계속할까요?',
 ar:'يتضمن الفهرس الأصلي ملفات اختيارية غير متاحة محليًا. سيؤدي حفظ تغييرات المرفقات إلى حذف هذه العناصر من الحزمة الجديدة. هل تريد المتابعة؟',
 ru:'Исходный индекс содержит недоступные локально необязательные файлы. Сохранение изменений вложений удалит эти записи из нового пакета. Продолжить?',
 de:'Der ursprüngliche Index enthält lokal nicht verfügbare optionale Dateien. Beim Speichern der Anhangänderungen werden diese Einträge aus dem neuen Paket entfernt. Fortfahren?',
 es:'El índice original contiene archivos opcionales no disponibles localmente. Guardar los cambios de adjuntos quitará esas entradas del nuevo paquete. ¿Continuar?',
 fr:'L’index original contient des fichiers facultatifs indisponibles localement. Enregistrer les modifications des pièces jointes retirera ces entrées du nouveau paquet. Continuer ?',
 it:'L’indice originale contiene file facoltativi non disponibili localmente. Salvare le modifiche agli allegati rimuoverà queste voci dal nuovo pacchetto. Continuare?',
};
export function trackSourceChangeConfirmation(locale){if(!labels[locale])throw Error('Missing source locale '+locale);return labels[locale];}
