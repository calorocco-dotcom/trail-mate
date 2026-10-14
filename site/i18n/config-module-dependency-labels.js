const labels={
 en:'PWM uses the device buzzer GPIO. This value is retained and used again when PWM is off.',
 'zh-Hans':'PWM 使用设备蜂鸣器 GPIO。此值保留，关闭 PWM 后再次使用。',
 'zh-Hant-TW':'PWM 使用裝置蜂鳴器 GPIO。此值保留，關閉 PWM 後再次使用。',
 ja:'PWM はデバイスのブザー GPIO を使用します。この値は保持され、PWM を無効にすると再び使用されます。',
 ko:'PWM은 기기의 부저 GPIO를 사용합니다. 이 값은 유지되며 PWM을 끄면 다시 사용됩니다.',
 ar:'يستخدم PWM منفذ GPIO الخاص بطنان الجهاز. تُحفظ هذه القيمة وتُستخدم مجددًا عند إيقاف PWM.',
 ru:'PWM использует GPIO зуммера устройства. Значение сохраняется и снова используется после отключения PWM.',
 de:'PWM verwendet den Summer-GPIO des Geräts. Dieser Wert bleibt erhalten und wird nach Abschalten von PWM wieder verwendet.',
 es:'PWM usa el GPIO del zumbador del dispositivo. Este valor se conserva y vuelve a usarse al desactivar PWM.',
 fr:'Le PWM utilise le GPIO de l’avertisseur de l’appareil. Cette valeur est conservée et réutilisée lorsque le PWM est désactivé.',
 it:'PWM usa il GPIO del cicalino del dispositivo. Questo valore viene conservato e riutilizzato quando PWM è disattivato.',
};
export function configModuleDependencyText(locale){if(!labels[locale])throw Error(`Missing module dependency text: ${locale}`);return labels[locale];}
