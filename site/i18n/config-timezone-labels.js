// Profile IDs and offsets follow core_sys timezone_profile.cpp. City profiles
// use the device's DST rules; this is not the browser's timezone database.
const offsets=[0,480,480,480,540,540,480,420,330,240,0,60,60,60,180,-300,-360,-420,-480,-420,-180,600,600,720];
const daylight=new Set([10,11,12,13,15,16,17,18,21,22,23]);
const cities={
 en:'UTC|Beijing|Taipei|Hong Kong|Tokyo|Seoul|Singapore|Bangkok|Kolkata|Dubai|London|Berlin|Paris|Rome|Moscow|US Eastern|US Central|US Mountain|US Pacific|Phoenix|São Paulo|Sydney|Melbourne|Auckland',
 'zh-Hans':'UTC|北京|台北|香港|东京|首尔|新加坡|曼谷|加尔各答|迪拜|伦敦|柏林|巴黎|罗马|莫斯科|美国东部|美国中部|美国山地|美国太平洋|凤凰城|圣保罗|悉尼|墨尔本|奥克兰',
 'zh-Hant-TW':'UTC|北京|臺北|香港|東京|首爾|新加坡|曼谷|加爾各答|杜拜|倫敦|柏林|巴黎|羅馬|莫斯科|美國東部|美國中部|美國山區|美國太平洋|鳳凰城|聖保羅|雪梨|墨爾本|奧克蘭',
 ja:'UTC|北京|台北|香港|東京|ソウル|シンガポール|バンコク|コルカタ|ドバイ|ロンドン|ベルリン|パリ|ローマ|モスクワ|米国東部|米国中部|米国山岳部|米国太平洋|フェニックス|サンパウロ|シドニー|メルボルン|オークランド',
 ko:'UTC|베이징|타이베이|홍콩|도쿄|서울|싱가포르|방콕|콜카타|두바이|런던|베를린|파리|로마|모스크바|미국 동부|미국 중부|미국 산악|미국 태평양|피닉스|상파울루|시드니|멜버른|오클랜드',
 ar:'UTC|بكين|تايبيه|هونغ كونغ|طوكيو|سيول|سنغافورة|بانكوك|كولكاتا|دبي|لندن|برلين|باريس|روما|موسكو|شرق الولايات المتحدة|وسط الولايات المتحدة|جبال الولايات المتحدة|ساحل المحيط الهادئ الأمريكي|فينيكس|ساو باولو|سيدني|ملبورن|أوكلاند',
 ru:'UTC|Пекин|Тайбэй|Гонконг|Токио|Сеул|Сингапур|Бангкок|Калькутта|Дубай|Лондон|Берлин|Париж|Рим|Москва|Восток США|Центр США|Горный пояс США|Тихоокеанский пояс США|Финикс|Сан-Паулу|Сидней|Мельбурн|Окленд',
 de:'UTC|Peking|Taipeh|Hongkong|Tokio|Seoul|Singapur|Bangkok|Kolkata|Dubai|London|Berlin|Paris|Rom|Moskau|US-Ostküste|US-Zentral|US-Bergregion|US-Pazifik|Phoenix|São Paulo|Sydney|Melbourne|Auckland',
 es:'UTC|Pekín|Taipéi|Hong Kong|Tokio|Seúl|Singapur|Bangkok|Calcuta|Dubái|Londres|Berlín|París|Roma|Moscú|Este de EE. UU.|Centro de EE. UU.|Montaña de EE. UU.|Pacífico de EE. UU.|Phoenix|São Paulo|Sídney|Melbourne|Auckland',
 fr:'UTC|Pékin|Taipei|Hong Kong|Tokyo|Séoul|Singapour|Bangkok|Calcutta|Dubaï|Londres|Berlin|Paris|Rome|Moscou|Est des États-Unis|Centre des États-Unis|Montagnes des États-Unis|Pacifique des États-Unis|Phoenix|São Paulo|Sydney|Melbourne|Auckland',
 it:'UTC|Pechino|Taipei|Hong Kong|Tokyo|Seul|Singapore|Bangkok|Calcutta|Dubai|Londra|Berlino|Parigi|Roma|Mosca|Est degli Stati Uniti|Centro degli Stati Uniti|Montagne degli Stati Uniti|Pacifico degli Stati Uniti|Phoenix|San Paolo|Sydney|Melbourne|Auckland',
};
const copy={
 en:['Fixed UTC offset','DST','Choose a device timezone profile. The displayed UTC offset is standard time; profiles marked DST follow the device’s daylight-saving rules.','The minute offset applies only to Fixed UTC offset. A city profile calculates its offset from the date.'],
 'zh-Hans':['固定 UTC 偏移','夏令时','选择设备时区。列表中的 UTC 偏移为标准时间；标有夏令时的配置按设备规则随日期调整。','分钟偏移仅在选择“固定 UTC 偏移”时生效；城市时区根据日期计算偏移。'],
 'zh-Hant-TW':['固定 UTC 偏移','日光節約時間','選擇裝置時區。清單中的 UTC 偏移為標準時間；標有日光節約時間的設定依裝置規則隨日期調整。','分鐘偏移僅在選擇「固定 UTC 偏移」時生效；城市時區依日期計算偏移。'],
 ja:['固定 UTC オフセット','夏時間','デバイスのタイムゾーンを選択します。UTC オフセットは標準時です。夏時間のある設定はデバイスの規則に従います。','分単位のオフセットは固定 UTC オフセットでのみ有効です。都市の設定では日付から計算します。'],
 ko:['고정 UTC 오프셋','서머타임','기기 시간대를 선택하세요. UTC 오프셋은 표준시이며 서머타임 설정은 기기 규칙에 따라 날짜별로 조정됩니다.','분 오프셋은 고정 UTC 오프셋에서만 적용됩니다. 도시 시간대는 날짜에 따라 계산합니다.'],
 ar:['إزاحة UTC ثابتة','التوقيت الصيفي','اختر منطقة زمنية للجهاز. إزاحة UTC المعروضة للتوقيت القياسي؛ تستخدم الخيارات الصيفية قواعد الجهاز.','تطبق الإزاحة بالدقائق فقط مع إزاحة UTC ثابتة. تحسب مناطق المدن الإزاحة حسب التاريخ.'],
 ru:['Фиксированное смещение UTC','Летнее время','Выберите часовой пояс устройства. Смещение UTC указано для стандартного времени; летнее время определяется правилами устройства.','Смещение в минутах действует только при фиксированном смещении UTC. Для города оно вычисляется по дате.'],
 de:['Fester UTC-Versatz','Sommerzeit','Zeitzone des Geräts wählen. Der UTC-Versatz zeigt die Normalzeit; Sommerzeit folgt den Regeln des Geräts.','Der Minutenversatz gilt nur bei festem UTC-Versatz. Ein Stadtprofil berechnet ihn anhand des Datums.'],
 es:['Desfase UTC fijo','Horario de verano','Elija la zona del dispositivo. El desfase UTC corresponde a la hora estándar; el horario de verano sigue las reglas del dispositivo.','El desfase en minutos solo se aplica al desfase UTC fijo. Los perfiles de ciudad lo calculan según la fecha.'],
 fr:['Décalage UTC fixe','Heure d’été','Choisissez le fuseau de l’appareil. Le décalage UTC indique l’heure standard ; l’heure d’été suit les règles de l’appareil.','Le décalage en minutes ne s’applique qu’au décalage UTC fixe. Un profil de ville le calcule selon la date.'],
 it:['Scarto UTC fisso','Ora legale','Scegli il fuso del dispositivo. Lo scarto UTC indica l’ora standard; l’ora legale segue le regole del dispositivo.','Lo scarto in minuti si applica solo allo scarto UTC fisso. Un profilo di città lo calcola in base alla data.'],
};
export function configTimezoneText(locale){
 const labels=cities[locale]?.split('|'),text=copy[locale];
 if(labels?.length!==offsets.length||text?.length!==4)throw Error(`Incomplete timezone catalog: ${locale}`);
 return {timezoneProfiles:[{value:-1,label:text[0]},...labels.map((city,value)=>{
  const minutes=offsets[value],offset=minutes===0?'UTC':`UTC${minutes<0?'−':'+'}${Math.floor(Math.abs(minutes)/60)}:${String(Math.abs(minutes)%60).padStart(2,'0')}`;
  return {value,label:`${city} · ${offset}${daylight.has(value)?` · ${text[1]}`:''}`};
 })],timezoneProfileHelp:text[2],timezoneOffsetHelp:text[3]};
}
