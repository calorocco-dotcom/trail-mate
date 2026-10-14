const copy={
 en:['Removing these parameters locally restores device defaults after saving. Use the relevant enable switch to turn the feature off.','Leaving this value blank restores the device default after saving.'],
 'zh-Hans':['移除这些本地参数后，设备会在保存时恢复默认配置。要关闭功能，请使用对应的启用开关。','留空后，设备会在保存时恢复默认值。'],
 'zh-Hant-TW':['移除這些本機參數後，裝置會在儲存時恢復預設設定。要關閉功能，請使用對應的啟用開關。','留空後，裝置會在儲存時恢復預設值。'],
 ja:['ここで設定を削除すると、保存時にデバイスの既定値が復元されます。機能を無効にするには、対応する有効化スイッチを使ってください。','空欄にすると、保存時にデバイスの既定値が復元されます。'],
 ko:['여기서 매개변수를 제거하면 저장 시 기기 기본 설정이 복원됩니다. 기능을 끄려면 해당 활성화 스위치를 사용하세요.','비워 두면 저장 시 기기 기본값이 복원됩니다.'],
 ar:['تؤدي إزالة هذه المعلمات محليًا إلى استعادة إعدادات الجهاز الافتراضية عند الحفظ. لإيقاف الميزة، استخدم مفتاح التفعيل المعني.','ترك هذه القيمة فارغة يستعيد القيمة الافتراضية للجهاز عند الحفظ.'],
 ru:['Удаление этих параметров здесь восстановит настройки устройства по умолчанию при сохранении. Для отключения функции используйте соответствующий переключатель.','Пустое значение будет заменено значением устройства по умолчанию при сохранении.'],
 de:['Das lokale Entfernen dieser Parameter stellt beim Speichern die Gerätestandards wieder her. Zum Ausschalten der Funktion den zugehörigen Aktivierungsschalter verwenden.','Ein leeres Feld wird beim Speichern durch den Gerätestandard ersetzt.'],
 es:['Al eliminar estos parámetros localmente, se restauran los valores predeterminados del dispositivo al guardar. Para desactivar la función, usa su interruptor de activación.','Si dejas este valor vacío, se restaura el valor predeterminado del dispositivo al guardar.'],
 fr:['La suppression locale de ces paramètres rétablit les valeurs par défaut de l’appareil lors de l’enregistrement. Pour désactiver la fonction, utilisez le commutateur correspondant.','Une valeur vide est remplacée par la valeur par défaut de l’appareil lors de l’enregistrement.'],
 it:['Rimuovendo questi parametri localmente, al salvataggio vengono ripristinati i valori predefiniti del dispositivo. Per disattivare la funzione, usa il relativo interruttore.','Lasciando vuoto questo valore, al salvataggio viene ripristinato il valore predefinito del dispositivo.'],
};
export function configModuleNormalizationText(locale){
 const values=copy[locale];if(!values||values.length!==2)throw Error(`Incomplete module normalization catalog: ${locale}`);
 return {restoredPresence:values[0],blankMqttValue:values[1]};
}
