export const configSchemaKeys=['integratedLoRa','tcpClient','direct','propagated','automatic','tcpHostHelp','gpsIntervalHelp','ifacBits'];
export const configSchemaLabels={
 en:['Integrated LoRa','TCP client','Direct','Propagation','Automatic','For TCP, enter a hostname or IP without a URL scheme, path or port.','Milliseconds; 60000 means one minute.','IFAC length (bits)'],
 'zh-Hans':['集成 LoRa','TCP 客户端','直连','传播','自动','TCP 接口使用主机名或 IP 地址，不包含 http://、路径或端口。','毫秒；60000 表示一分钟。','IFAC 长度（位）'],
 'zh-Hant-TW':['整合 LoRa','TCP 用戶端','直連','傳播','自動','TCP 介面使用主機名稱或 IP 位址，不包含 URL 協定、路徑或連接埠。','毫秒；60000 表示一分鐘。','IFAC 長度（位元）'],
 ja:['内蔵 LoRa','TCP クライアント','直接','伝播','自動','TCP には URL スキーム、パス、ポートを含まないホスト名または IP を入力してください。','ミリ秒単位。60000 は 1 分です。','IFAC 長さ（ビット）'],
 ko:['통합 LoRa','TCP 클라이언트','직접','전파','자동','TCP에는 URL 스킴, 경로, 포트 없이 호스트 이름이나 IP 주소를 입력하세요.','밀리초 단위이며 60000은 1분입니다.','IFAC 길이(비트)'],
 ar:['LoRa مدمج','عميل TCP','مباشر','انتشار','تلقائي','لـ TCP أدخل اسم مضيف أو عنوان IP دون مخطط URL أو مسار أو منفذ.','بالمللي ثانية؛ 60000 تعني دقيقة واحدة.','طول IFAC (بت)'],
 ru:['Встроенный LoRa','Клиент TCP','Прямо','Распространение','Автоматически','Для TCP укажите имя хоста или IP без схемы URL, пути и порта.','Миллисекунды; 60000 означает одну минуту.','Длина IFAC (бит)'],
 de:['Integriertes LoRa','TCP-Client','Direkt','Propagation','Automatisch','Für TCP Hostnamen oder IP ohne URL-Schema, Pfad oder Port eingeben.','Millisekunden; 60000 entspricht einer Minute.','IFAC-Länge (Bit)'],
 es:['LoRa integrado','Cliente TCP','Directa','Propagación','Automática','Para TCP, introduce un nombre de host o IP sin esquema URL, ruta ni puerto.','Milisegundos; 60000 equivale a un minuto.','Longitud IFAC (bits)'],
 fr:['LoRa intégré','Client TCP','Direct','Propagation','Automatique','Pour TCP, saisissez un nom d’hôte ou une IP sans schéma URL, chemin ni port.','Millisecondes ; 60000 correspond à une minute.','Longueur IFAC (bits)'],
 it:['LoRa integrato','Client TCP','Diretta','Propagazione','Automatica','Per TCP, inserisci un nome host o IP senza schema URL, percorso o porta.','Millisecondi; 60000 equivale a un minuto.','Lunghezza IFAC (bit)'],
};
export function configSchemaText(locale){const values=configSchemaLabels[locale];if(!values||values.length!==configSchemaKeys.length)throw Error(`Incomplete config schema catalog: ${locale}`);return Object.fromEntries(configSchemaKeys.map((key,index)=>[key,values[index]]));}
