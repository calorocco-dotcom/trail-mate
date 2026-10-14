const labels={
 'zh-Hans':'图片裁剪参数无效或超出已验证图片范围；原文保留，未绘制裁剪图片。',
 en:'Invalid crop parameters or crop outside the verified image; original retained and cropped image not rendered.',
 'zh-Hant-TW':'圖片裁切參數無效或超出已驗證圖片範圍；原文保留，未繪製裁切圖片。',
 ja:'切り抜き設定が無効、または検証済み画像の範囲外です。原文を保持し、切り抜き画像は表示しません。',
 ko:'자르기 설정이 잘못되었거나 검증된 이미지 범위를 벗어납니다. 원본은 보존하며 잘린 이미지는 표시하지 않습니다.',
 ar:'معلمات القص غير صالحة أو خارج الصورة الموثّقة؛ يُحفظ الأصل ولا تُعرض الصورة المقصوصة.',
 ru:'Недопустимая обрезка или выход за границы проверенного изображения; оригинал сохранён, обрезка не показана.',
 de:'Ungültiger Zuschnitt oder außerhalb des geprüften Bildes; Original bleibt erhalten, Ausschnitt wird nicht gezeichnet.',
 es:'Recorte no válido o fuera de la imagen verificada; se conserva el original y no se dibuja el recorte.',
 fr:'Recadrage invalide ou hors de l’image vérifiée ; original conservé, image recadrée non affichée.',
 it:'Ritaglio non valido o fuori dall’immagine verificata; originale conservato, ritaglio non mostrato.',
};
export function kmlCropText(locale){return labels[locale]??labels.en;}
