// Common primary-table symbols verified against the APRS master table.
export const APRS_PRIMARY_SYMBOLS=['[','b',';','<','>','C','e','j','s','-','_','r'];
const rows={
 'zh-Hans':['人员／步行','自行车','营地','摩托车','汽车','独木舟','骑马','吉普车','动力船','固定住宅站','气象站','中继站'],
 'zh-Hant-TW':['人員／步行','自行車','營地','摩托車','汽車','獨木舟','騎馬','吉普車','動力船','固定住宅站','氣象站','中繼站'],
 en:['Person / on foot','Bicycle','Campground','Motorcycle','Car','Canoe','Horse','Jeep','Powerboat','Home station','Weather station','Repeater'],
 ja:['人／徒歩','自転車','キャンプ場','オートバイ','自動車','カヌー','馬','ジープ','動力船','自宅局','気象局','中継局'],
 ko:['사람 / 도보','자전거','캠핑장','오토바이','자동차','카누','말','지프','동력선','자택 기지','기상 관측소','중계기'],
 ar:['شخص / مشي','دراجة','مخيم','دراجة نارية','سيارة','زورق','حصان','جيب','قارب بمحرك','محطة منزلية','محطة طقس','مكرر'],
 ru:['Человек / пешком','Велосипед','Кемпинг','Мотоцикл','Автомобиль','Каноэ','Лошадь','Джип','Моторная лодка','Домашняя станция','Метеостанция','Ретранслятор'],
 de:['Person / zu Fuß','Fahrrad','Campingplatz','Motorrad','Auto','Kanu','Pferd','Jeep','Motorboot','Heimstation','Wetterstation','Repeater'],
 es:['Persona / a pie','Bicicleta','Camping','Motocicleta','Coche','Canoa','Caballo','Jeep','Lancha a motor','Estación doméstica','Estación meteorológica','Repetidor'],
 fr:['Personne / à pied','Vélo','Camping','Moto','Voiture','Canoë','Cheval','Jeep','Bateau à moteur','Station domestique','Station météo','Relais'],
 it:['Persona / a piedi','Bicicletta','Campeggio','Motocicletta','Auto','Canoa','Cavallo','Jeep','Barca a motore','Stazione domestica','Stazione meteo','Ripetitore'],
};
export function configAprsSymbolNames(locale){const row=rows[locale]??rows.en;return Object.fromEntries(APRS_PRIMARY_SYMBOLS.map((character,index)=>[character,row[index]]));}
