const keys=['custom','narrow','long','medium','recommended','help'];
const rows={
 'zh-Hans':['自定义','窄带','远距离','中距离','推荐','选择预设会一起设置频率、带宽、扩频因子、编码率和发射功率。单独修改前四项会切换为自定义；发射功率可独立调整。'],
 'zh-Hant-TW':['自訂','窄頻','遠距離','中距離','建議','選取預設會一起設定頻率、頻寬、展頻因子、編碼率與發射功率。單獨修改前四項會切換為自訂；發射功率可獨立調整。'],
 en:['Custom','Narrow','Long range','Medium range','Recommended','Selecting a preset sets frequency, bandwidth, spreading factor, coding rate and transmit power together. Editing the first four switches to Custom; power can be adjusted independently.'],
 ja:['カスタム','狭帯域','長距離','中距離','推奨','プリセットで周波数、帯域幅、拡散率、符号化率、送信電力をまとめて設定します。最初の4項目を個別変更するとカスタムになります。電力は独立して調整できます。'],
 ko:['사용자 지정','협대역','장거리','중거리','권장','프리셋은 주파수, 대역폭, 확산 계수, 코딩률, 송신 출력을 함께 설정합니다. 처음 네 항목을 따로 바꾸면 사용자 지정으로 전환됩니다. 출력은 별도로 조정할 수 있습니다.'],
 ar:['مخصص','نطاق ضيق','مدى بعيد','مدى متوسط','موصى به','يضبط الإعداد المسبق التردد وعرض النطاق ومعامل الانتشار ومعدل الترميز وقدرة الإرسال معًا. تغيير أحد الأربعة الأولى يحوّل إلى مخصص؛ يمكن ضبط القدرة مستقلة.'],
 ru:['Свой вариант','Узкая полоса','Дальняя связь','Средняя дальность','Рекомендуется','Пресет задаёт частоту, полосу, фактор расширения, кодирование и мощность вместе. Изменение первых четырёх включает свой вариант; мощность регулируется независимо.'],
 de:['Benutzerdefiniert','Schmalband','Große Reichweite','Mittlere Reichweite','Empfohlen','Die Vorgabe setzt Frequenz, Bandbreite, Spreizfaktor, Codierrate und Sendeleistung gemeinsam. Änderungen der ersten vier wechseln zu Benutzerdefiniert; die Leistung ist unabhängig einstellbar.'],
 es:['Personalizado','Banda estrecha','Largo alcance','Alcance medio','Recomendado','El perfil ajusta frecuencia, ancho de banda, factor de expansión, codificación y potencia juntos. Cambiar uno de los cuatro primeros activa Personalizado; la potencia se ajusta por separado.'],
 fr:['Personnalisé','Bande étroite','Longue portée','Portée moyenne','Recommandé','Le profil règle ensemble fréquence, bande passante, facteur d’étalement, codage et puissance. Modifier les quatre premiers active Personnalisé ; la puissance reste indépendante.'],
 it:['Personalizzato','Banda stretta','Lunga portata','Media portata','Consigliato','Il profilo imposta insieme frequenza, banda, fattore di diffusione, codifica e potenza. Modificare i primi quattro attiva Personalizzato; la potenza resta indipendente.'],
};
export function configMeshCoreRegionText(locale){const row=rows[locale]??rows.en;return Object.fromEntries(keys.map((key,index)=>[key,row[index]]));}
