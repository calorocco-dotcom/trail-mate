const labels={
 en:'Shared Wi-Fi is enabled and the C6 STA SSID is set, so C6 Wi-Fi and STA will be enabled after restart even if these switches are off. Disable shared Wi-Fi to use the C6 switches independently.',
 'zh-Hans':'共享 Wi-Fi 已启用且 C6 STA 网络名称非空，因此重启后 C6 Wi-Fi 和 STA 会启用，即使这两个开关关闭。关闭共享 Wi-Fi 后，C6 开关才会独立生效。',
 'zh-Hant-TW':'共用 Wi-Fi 已啟用且 C6 STA 網路名稱非空，因此重新啟動後 C6 Wi-Fi 和 STA 會啟用，即使這兩個開關關閉。關閉共用 Wi-Fi 後，C6 開關才會獨立生效。',
 ja:'共有 Wi-Fi が有効で C6 STA の SSID が設定されているため、スイッチがオフでも再起動後に C6 Wi-Fi と STA が有効になります。個別に制御するには共有 Wi-Fi を無効にしてください。',
 ko:'공유 Wi-Fi가 켜져 있고 C6 STA SSID가 설정되어 있어, 이 스위치가 꺼져 있어도 재시작 후 C6 Wi-Fi와 STA가 켜집니다. C6 스위치를 독립적으로 사용하려면 공유 Wi-Fi를 끄세요.',
 ar:'شبكة Wi-Fi المشتركة مفعلة واسم شبكة C6 STA محدد؛ لذلك سيتفعل C6 Wi-Fi وSTA بعد إعادة التشغيل حتى لو كان المفتاحان مغلقين. عطّل Wi-Fi المشتركة للتحكم بهما بشكل مستقل.',
 ru:'Общий Wi-Fi включён и SSID C6 STA задан, поэтому после перезапуска C6 Wi-Fi и STA включатся даже при выключенных переключателях. Отключите общий Wi-Fi для независимого управления C6.',
 de:'Gemeinsames Wi-Fi ist aktiv und die C6-STA-SSID ist gesetzt. Daher werden C6 Wi-Fi und STA nach dem Neustart auch bei ausgeschalteten Schaltern aktiviert. Für unabhängige C6-Schalter gemeinsames Wi-Fi ausschalten.',
 es:'El Wi-Fi compartido está activo y el SSID de C6 STA está configurado. C6 Wi-Fi y STA se activarán tras reiniciar aunque estos interruptores estén apagados. Desactiva el Wi-Fi compartido para controlar C6 de forma independiente.',
 fr:'Le Wi-Fi partagé est actif et le SSID C6 STA est défini. C6 Wi-Fi et STA seront activés après redémarrage même si ces interrupteurs sont désactivés. Désactivez le Wi-Fi partagé pour contrôler C6 indépendamment.',
 it:'Il Wi-Fi condiviso è attivo e il SSID C6 STA è impostato. C6 Wi-Fi e STA si attiveranno dopo il riavvio anche con questi interruttori spenti. Disattiva il Wi-Fi condiviso per controllare C6 in modo indipendente.',
};
export function configC6EffectiveText(locale){if(!labels[locale])throw Error('Missing C6 effective state locale: '+locale);return labels[locale];}
