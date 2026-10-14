const labels={
 en:'An empty AP password creates an open network. When the AP is enabled, a non-empty WPA2 password must contain 8–63 UTF-8 bytes.',
 'zh-Hans':'AP 密码留空表示开放网络。启用 AP 时，非空 WPA2 密码必须为 8–63 个 UTF-8 字节。',
 'zh-Hant-TW':'AP 密碼留空表示開放網路。啟用 AP 時，非空 WPA2 密碼必須為 8–63 個 UTF-8 位元組。',
 ja:'AP パスワードが空の場合はオープンネットワークになります。AP を有効にする場合、WPA2 パスワードは 8～63 UTF-8 バイト必要です。',
 ko:'AP 비밀번호가 비어 있으면 개방형 네트워크가 됩니다. AP를 켤 때 비어 있지 않은 WPA2 비밀번호는 UTF-8 기준 8–63바이트여야 합니다.',
 ar:'ترك كلمة مرور AP فارغة ينشئ شبكة مفتوحة. عند تفعيل AP، يجب أن تكون كلمة مرور WPA2 غير الفارغة بين 8 و63 بايت UTF-8.',
 ru:'Пустой пароль AP создаёт открытую сеть. При включении AP непустой пароль WPA2 должен содержать 8–63 байта UTF-8.',
 de:'Ein leeres AP-Passwort erzeugt ein offenes Netzwerk. Bei aktivem AP muss ein WPA2-Passwort 8–63 UTF-8-Bytes enthalten.',
 es:'Una contraseña AP vacía crea una red abierta. Con el AP activo, una contraseña WPA2 no vacía debe ocupar 8–63 bytes UTF-8.',
 fr:'Un mot de passe AP vide crée un réseau ouvert. Lorsque l’AP est actif, un mot de passe WPA2 non vide doit contenir 8 à 63 octets UTF-8.',
 it:'Una password AP vuota crea una rete aperta. Con AP attivo, una password WPA2 non vuota deve contenere 8–63 byte UTF-8.',
};
export function configC6PasswordText(locale){if(!labels[locale])throw Error('Missing C6 password locale: '+locale);return labels[locale];}
