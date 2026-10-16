const names={
 en:['Positioning','Receiver initialization','Motion detection','External position output'],
 'zh-Hans':['基本定位','接收器初始化','运动检测','外部定位输出'],
 'zh-Hant-TW':['基本定位','接收器初始化','運動偵測','外部定位輸出'],
 ja:['基本測位','受信機の初期化','動きの検出','外部への位置出力'],
 ko:['기본 위치 확인','수신기 초기화','움직임 감지','외부 위치 출력'],
 ar:['تحديد الموقع','تهيئة المستقبل','اكتشاف الحركة','إخراج الموقع الخارجي'],
 ru:['Определение положения','Инициализация приёмника','Определение движения','Внешний вывод положения'],
 de:['Positionierung','Empfängerinitialisierung','Bewegungserkennung','Externe Positionsausgabe'],
 es:['Posicionamiento','Inicialización del receptor','Detección de movimiento','Salida de posición externa'],
 fr:['Positionnement','Initialisation du récepteur','Détection du mouvement','Sortie de position externe'],
 it:['Posizionamento','Inizializzazione del ricevitore','Rilevamento del movimento','Uscita posizione esterna'],
};
export function configSectionText(locale){const values=names[locale];if(!values||values.length!==4)throw Error('Incomplete setting sections');return Object.fromEntries(['position','startup','motion','output'].map((key,index)=>[key,values[index]]));}
