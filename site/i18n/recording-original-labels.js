const labels={
 en:['Download original recording','The imported GPX, CSV or BIN is kept separately with its original name and bytes. Editing changes the KML draft. Draft backups retain both; both count toward browser storage. The recording is not automatically shared.'],
 'zh-Hans':['下载原始记录','导入的 GPX、CSV 或 BIN 按原名和原字节独立保留，编辑只改变 KML 草稿。草稿备份保留两份内容，均计入浏览器存储容量；原记录不会自动共享。'],
 'zh-Hant-TW':['下載原始記錄','匯入的 GPX、CSV 或 BIN 依原名與原位元組獨立保留，編輯只變更 KML 草稿。草稿備份保留兩份內容，均計入瀏覽器儲存容量；原記錄不會自動分享。'],
 ja:['元の記録をダウンロード','読み込んだGPX・CSV・BINを元の名前とバイトで別に保持します。編集はKML下書きだけを変更します。バックアップは両方を保持し、両方が保存容量に含まれます。元の記録は自動共有されません。'],
 ko:['원본 기록 다운로드','가져온 GPX, CSV 또는 BIN을 원래 이름과 바이트로 따로 보관합니다. 편집은 KML 초안만 바꿉니다. 백업은 두 파일을 보존하며 모두 저장 용량에 포함됩니다. 원본 기록은 자동으로 공유되지 않습니다.'],
 ar:['تنزيل التسجيل الأصلي','يُحفظ GPX أو CSV أو BIN المستورد منفصلًا باسمه وبايتاته الأصلية. يغيّر التحرير مسودة KML فقط. تحتفظ النسخة الاحتياطية بالملفين، ويُحتسبان ضمن مساحة المتصفح. لا يُشارك التسجيل تلقائيًا.'],
 ru:['Скачать исходную запись','Импортированный GPX, CSV или BIN сохраняется отдельно с исходным именем и байтами. Правки меняют черновик KML. Резервная копия сохраняет оба файла; оба учитываются в хранилище. Запись не публикуется автоматически.'],
 de:['Originalaufzeichnung herunterladen','Importierte GPX-, CSV- oder BIN-Dateien bleiben mit ursprünglichem Namen und Bytes separat erhalten. Änderungen betreffen den KML-Entwurf. Sicherungen enthalten beide Dateien; beide zählen zum Browserspeicher. Die Aufzeichnung wird nicht automatisch geteilt.'],
 es:['Descargar registro original','El GPX, CSV o BIN importado se conserva aparte con su nombre y bytes originales. La edición cambia el borrador KML. La copia conserva ambos archivos y ambos cuentan en el almacenamiento. El registro no se comparte automáticamente.'],
 fr:['Télécharger l’enregistrement original','Le GPX, CSV ou BIN importé est conservé séparément avec son nom et ses octets d’origine. Les modifications concernent le brouillon KML. La sauvegarde conserve les deux fichiers, comptés dans le stockage. L’enregistrement n’est pas partagé automatiquement.'],
 it:['Scarica registrazione originale','Il GPX, CSV o BIN importato resta separato con nome e byte originali. Le modifiche cambiano la bozza KML. Il backup conserva entrambi i file, conteggiati nello spazio del browser. La registrazione non viene condivisa automaticamente.'],
};
export function recordingOriginalText(locale){const values=labels[locale];if(!values||values.length!==2)throw Error('Incomplete original recording translations');return{download:values[0],help:values[1]};}
