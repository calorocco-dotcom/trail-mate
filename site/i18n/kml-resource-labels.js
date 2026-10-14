const copy={
 en:['Other KML contents','Resource reference','These contents remain in the original file. Ground overlays can use verified local attachments; other contents may have limited previews. External references are not accessed automatically.'],
 'zh-Hans':['其他 KML 内容','资源引用','这些内容保留在原文件中。地理图片叠加可使用已验证的本地附件，其他内容可能仅提供部分预览；不会自动访问外部引用。'],
 'zh-Hant-TW':['其他 KML 內容','資源參照','這些內容保留在原始檔案中。地理圖片疊加可使用已驗證的本機附件，其他內容可能僅提供部分預覽；不會自動存取外部參照。'],
 ja:['その他の KML 内容','リソース参照','内容は原文に保持されます。地理画像には検証済みのローカル添付を使用します。他の内容は一部のみプレビュー可能です。外部参照には自動アクセスしません。'],
 ko:['기타 KML 내용','리소스 참조','내용은 원본에 보존됩니다. 지리 이미지는 검증된 로컬 첨부를 사용하며 다른 내용은 일부만 미리보기할 수 있습니다. 외부 참조에 자동 접근하지 않습니다.'],
 ar:['محتويات KML الأخرى','مرجع المورد','يُحفظ المحتوى الأصلي. تستخدم التراكبات مرفقات محلية موثّقة؛ قد تكون معاينة العناصر الأخرى محدودة. لا تُفتح المراجع الخارجية تلقائيًا.'],
 ru:['Другие элементы KML','Ссылка на ресурс','Оригинал сохранён. Наложения используют проверенные локальные вложения; другие элементы могут отображаться частично. Внешние ссылки автоматически не открываются.'],
 de:['Weitere KML-Inhalte','Ressourcenreferenz','Das Original bleibt erhalten. Overlays nutzen geprüfte lokale Anhänge; andere Inhalte haben eventuell eingeschränkte Vorschauen. Externe Referenzen werden nicht automatisch geöffnet.'],
 es:['Otros contenidos KML','Referencia de recurso','Se conserva el original. Las superposiciones usan adjuntos locales verificados; otros contenidos pueden tener vistas limitadas. Las referencias externas no se abren automáticamente.'],
 fr:['Autres contenus KML','Référence de ressource','L’original est conservé. Les superpositions utilisent des pièces jointes locales vérifiées ; les autres aperçus peuvent être limités. Les références externes ne sont pas ouvertes automatiquement.'],
 it:['Altri contenuti KML','Riferimento alla risorsa','L’originale è conservato. Le sovrapposizioni usano allegati locali verificati; le altre anteprime possono essere limitate. I riferimenti esterni non vengono aperti automaticamente.'],
};
export function kmlResourceText(locale){
 const values=copy[locale];if(!values||values.length!==3)throw Error(`Incomplete KML resource catalog: ${locale}`);
 return {title:values[0],reference:values[1],notice:values[2]};
}
