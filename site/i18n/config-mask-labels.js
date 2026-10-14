const labels={en:'Select satellite systems, or edit the raw mask below. Unknown bits are retained.',
 'zh-Hans':'勾选卫星系统，或编辑下方原始掩码数值；未知位会保留。',
 'zh-Hant-TW':'勾選衛星系統，或編輯下方原始遮罩數值；未知位元會保留。',
 ja:'衛星システムを選ぶか、下のマスク値を編集します。未知のビットは保持されます。',
 ko:'위성 시스템을 선택하거나 아래 마스크 값을 편집하세요. 알 수 없는 비트는 유지됩니다.',
 ar:'اختر أنظمة الأقمار الصناعية أو عدّل قيمة القناع أدناه. تُحفظ البتات غير المعروفة.',
 ru:'Выберите спутниковые системы или измените маску ниже. Неизвестные биты сохраняются.',
 de:'Satellitensysteme wählen oder den Maskenwert unten bearbeiten. Unbekannte Bits bleiben erhalten.',
 es:'Seleccione sistemas de satélites o edite la máscara inferior. Se conservan los bits desconocidos.',
 fr:'Sélectionnez les systèmes de satellites ou modifiez le masque ci-dessous. Les bits inconnus sont conservés.',
 it:'Seleziona i sistemi satellitari o modifica la maschera sotto. I bit sconosciuti vengono mantenuti.'};
const advanced={en:'Advanced: raw satellite mask','zh-Hans':'高级：卫星系统原始掩码','zh-Hant-TW':'進階：衛星系統原始遮罩',ja:'詳細設定：衛星システムのマスク値',ko:'고급: 위성 시스템 원시 마스크',ar:'متقدم: قناع أنظمة الأقمار الصناعية',ru:'Дополнительно: исходная маска спутниковых систем',de:'Erweitert: Rohmaske der Satellitensysteme',es:'Avanzado: máscara de sistemas de satélites',fr:'Avancé : masque des systèmes de satellites',it:'Avanzate: maschera dei sistemi satellitari'};
export function configMaskHelp(locale,kind='help'){if(!labels[locale]||!advanced[locale])throw Error('Incomplete mask translations');return kind==='advanced'?advanced[locale]:labels[locale];}
