const keys=['title','field','before','after','empty','missing','secret','binary','enabled','disabled','unsupported'];
const values={
 en:['Unsaved changes','Setting','Device value','Edited value','Empty','Not present','Sensitive value hidden','Binary content changed','Enabled','Disabled','Unsupported value'],
 'zh-Hans':['未保存修改对比','设置','设备原值','编辑后值','空值','不存在','敏感内容已隐藏','二进制内容有变更','启用','停用','不支持显示的值'],
 'zh-Hant-TW':['未儲存修改比較','設定','裝置原值','編輯後值','空值','不存在','敏感內容已隱藏','二進位內容有變更','啟用','停用','不支援顯示的值'],
 ja:['未保存の変更','設定','デバイスの値','編集後の値','空','存在しません','機密値は非表示','バイナリ内容を変更','有効','無効','表示できない値'],
 ko:['저장하지 않은 변경','설정','기기 원래 값','편집한 값','비어 있음','없음','민감한 값 숨김','바이너리 콘텐츠 변경','활성','비활성','표시할 수 없는 값'],
 ar:['التغييرات غير المحفوظة','الإعداد','قيمة الجهاز','القيمة المعدلة','فارغ','غير موجود','القيمة الحساسة مخفية','تغيّر المحتوى الثنائي','مفعّل','معطّل','قيمة غير مدعومة'],
 ru:['Несохранённые изменения','Настройка','Значение на устройстве','Изменённое значение','Пусто','Отсутствует','Секретное значение скрыто','Двоичное содержимое изменено','Включено','Выключено','Неподдерживаемое значение'],
 de:['Ungespeicherte Änderungen','Einstellung','Gerätewert','Geänderter Wert','Leer','Nicht vorhanden','Vertraulicher Wert verborgen','Binärinhalt geändert','Aktiviert','Deaktiviert','Nicht unterstützter Wert'],
 es:['Cambios sin guardar','Ajuste','Valor del dispositivo','Valor editado','Vacío','No existe','Valor sensible oculto','Contenido binario modificado','Activado','Desactivado','Valor no compatible'],
 fr:['Modifications non enregistrées','Réglage','Valeur de l’appareil','Valeur modifiée','Vide','Absent','Valeur sensible masquée','Contenu binaire modifié','Activé','Désactivé','Valeur non prise en charge'],
 it:['Modifiche non salvate','Impostazione','Valore del dispositivo','Valore modificato','Vuoto','Non presente','Valore sensibile nascosto','Contenuto binario modificato','Attivo','Disattivo','Valore non supportato'],
};
export function configReviewText(locale){const labels=values[locale];if(!labels||labels.length!==keys.length)throw Error('Incomplete configuration review translations');return Object.fromEntries(keys.map((key,index)=>[key,labels[index]]));}
