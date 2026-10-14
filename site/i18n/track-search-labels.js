const keys=['all','help','unsupported','invalid'];
const copy={
 en:['All activities','One tag per line. Every tag must match exactly; case matters.','This directory does not support activity and tag filters. Choose another directory to use them.','Use a valid activity and up to 8 distinct tags of at most 48 UTF-8 bytes each.'],
 'zh-Hans':['全部活动','每行一个标签，所有标签必须精确匹配，区分大小写。','这个目录尚不支持活动与标签筛选，选择其他目录后可用。','请选择有效活动，最多8个不同标签，每个不超过48个UTF-8字节。'],
 'zh-Hant-TW':['全部活動','每行一個標籤，所有標籤須完全符合，區分大小寫。','此目錄尚不支援活動與標籤篩選，請選擇其他目錄。','請選擇有效活動，最多8個不同標籤，每個不超過48個UTF-8位元組。'],
 ja:['すべての種別','1行に1タグ。すべてのタグが完全一致する必要があり、大文字と小文字を区別します。','このディレクトリは種別とタグの絞り込みに未対応です。別のディレクトリを選択してください。','有効な種別と、各48 UTF-8バイト以内の異なるタグを8個まで指定してください。'],
 ko:['모든 활동','한 줄에 태그 하나를 입력하세요. 모든 태그가 대소문자까지 정확히 일치해야 합니다.','이 디렉터리는 활동 및 태그 필터를 지원하지 않습니다. 다른 디렉터리를 선택하세요.','유효한 활동과 각각 UTF-8 48바이트 이하인 서로 다른 태그를 최대 8개 입력하세요.'],
 ar:['كل الأنشطة','وسم واحد في كل سطر. يجب أن تتطابق جميع الوسوم تمامًا مع مراعاة حالة الأحرف.','هذا الدليل لا يدعم تصفية النشاط والوسوم. اختر دليلًا آخر.','اختر نشاطًا صالحًا وما يصل إلى 8 وسوم مختلفة، كل منها لا يتجاوز 48 بايت UTF-8.'],
 ru:['Все занятия','Один тег в строке. Все теги должны точно совпадать с учётом регистра.','Каталог не поддерживает фильтры занятия и тегов. Выберите другой каталог.','Укажите допустимое занятие и до 8 различных тегов, не более 48 байт UTF-8 каждый.'],
 de:['Alle Aktivitäten','Ein Tag pro Zeile. Alle Tags müssen genau übereinstimmen; Groß-/Kleinschreibung beachten.','Dieses Verzeichnis unterstützt keine Aktivitäts- und Tagfilter. Wählen Sie ein anderes Verzeichnis.','Gültige Aktivität und bis zu 8 verschiedene Tags mit jeweils höchstens 48 UTF-8-Bytes angeben.'],
 es:['Todas las actividades','Una etiqueta por línea. Todas deben coincidir exactamente, incluidas mayúsculas y minúsculas.','Este directorio no admite filtros de actividad y etiquetas. Elige otro directorio.','Usa una actividad válida y hasta 8 etiquetas distintas de un máximo de 48 bytes UTF-8 cada una.'],
 fr:['Toutes les activités','Une étiquette par ligne. Toutes doivent correspondre exactement, majuscules comprises.','Ce répertoire ne prend pas en charge ces filtres. Choisissez un autre répertoire.','Choisissez une activité valide et jusqu’à 8 étiquettes distinctes de 48 octets UTF-8 maximum chacune.'],
 it:['Tutte le attività','Un tag per riga. Devono corrispondere tutti esattamente, comprese maiuscole e minuscole.','Questa directory non supporta filtri per attività e tag. Scegline un’altra.','Usa un’attività valida e fino a 8 tag diversi di massimo 48 byte UTF-8 ciascuno.']
};
export function trackSearchText(locale){const values=copy[locale];if(values?.length!==keys.length)throw Error('Incomplete track search locale: '+locale);return Object.fromEntries(keys.map((key,index)=>[key,values[index]]));}
