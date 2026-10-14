const primaryHelp={
 en:'The first MeshCore channel stays enabled. You can edit its name and key.',
 'zh-Hans':'MeshCore 第一个频道始终启用，可以修改它的名称和密钥。',
 'zh-Hant-TW':'MeshCore 第一個頻道始終啟用，可以修改它的名稱和金鑰。',
 ja:'MeshCore の最初のチャンネルは常に有効です。名前と鍵は編集できます。',
 ko:'첫 번째 MeshCore 채널은 항상 활성화됩니다. 이름과 키는 편집할 수 있습니다.',
 ar:'تظل قناة MeshCore الأولى مفعّلة. يمكنك تعديل اسمها ومفتاحها.',
 ru:'Первый канал MeshCore всегда включён. Его имя и ключ можно изменить.',
 de:'Der erste MeshCore-Kanal bleibt aktiviert. Name und Schlüssel lassen sich bearbeiten.',
 es:'El primer canal de MeshCore permanece activado. Puedes editar su nombre y su clave.',
 fr:'Le premier canal MeshCore reste activé. Vous pouvez modifier son nom et sa clé.',
 it:'Il primo canale MeshCore resta attivo. Puoi modificarne il nome e la chiave.',
};
const mirrorHelp={
 en:'This compatibility value follows the active MeshCore channel. Edit that channel’s key instead.',
 'zh-Hans':'此兼容值来自当前 MeshCore 频道。请修改对应频道的密钥。',
 'zh-Hant-TW':'此相容值來自目前 MeshCore 頻道。請修改對應頻道的金鑰。',
 ja:'この互換値は現在の MeshCore チャンネルから取得されます。そのチャンネルの鍵を編集してください。',
 ko:'이 호환 값은 현재 MeshCore 채널에서 가져옵니다. 해당 채널의 키를 수정하세요.',
 ar:'تتبع قيمة التوافق هذه قناة MeshCore النشطة. عدّل مفتاح تلك القناة.',
 ru:'Это значение совместимости берётся из активного канала MeshCore. Измените ключ этого канала.',
 de:'Dieser Kompatibilitätswert stammt vom aktiven MeshCore-Kanal. Bearbeite den Schlüssel dieses Kanals.',
 es:'Este valor de compatibilidad procede del canal MeshCore activo. Edita la clave de ese canal.',
 fr:'Cette valeur de compatibilité provient du canal MeshCore actif. Modifiez la clé de ce canal.',
 it:'Questo valore di compatibilità deriva dal canale MeshCore attivo. Modifica la chiave di quel canale.',
};
const slotLabel={en:'Channel {slot}','zh-Hans':'频道 {slot}','zh-Hant-TW':'頻道 {slot}',ja:'チャンネル {slot}',ko:'채널 {slot}',ar:'القناة {slot}',ru:'Канал {slot}',de:'Kanal {slot}',es:'Canal {slot}',fr:'Canal {slot}',it:'Canale {slot}'};
export function configMeshChannelText(locale){
 if(!Object.hasOwn(primaryHelp,locale)||!Object.hasOwn(mirrorHelp,locale)||!Object.hasOwn(slotLabel,locale))throw Error(`Incomplete MeshCore channel catalog: ${locale}`);
 const number=new Intl.NumberFormat(locale);
 return {meshCorePrimaryHelp:primaryHelp[locale],meshCoreMirrorHelp:mirrorHelp[locale],meshCoreSlots:Array.from({length:8},(_,value)=>({value,label:slotLabel[locale].replace('{slot}',number.format(value+1))}))};
}
