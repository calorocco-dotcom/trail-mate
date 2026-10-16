const labels={
 en:['The directory cannot access its storage right now. Retry later or choose another directory.','The directory cannot access its storage right now. Publication has not been confirmed. Your original task is retained; use “{resume}” later to retry without signing again.'],
 'zh-Hans':['目录暂时无法访问存储。请稍后重试，或选择其他目录。','目录暂时无法访问存储，发布尚未获得成功确认。原任务已保留，请稍后点击“{resume}”，无需重新签名。'],
 'zh-Hant-TW':['目錄暫時無法存取儲存空間。請稍後重試，或選擇其他目錄。','目錄暫時無法存取儲存空間，發布尚未獲得成功確認。原任務已保留，請稍後點擊「{resume}」，不必重新簽署。'],
 ja:['ディレクトリが現在ストレージにアクセスできません。後で再試行するか、別のディレクトリを選んでください。','ディレクトリが現在ストレージにアクセスできず、公開は確認されていません。元のタスクを保持しています。後で「{resume}」から、再署名せずに再試行できます。'],
 ko:['디렉터리가 현재 저장소에 접근할 수 없습니다. 나중에 다시 시도하거나 다른 디렉터리를 선택하세요.','디렉터리가 현재 저장소에 접근할 수 없어 게시가 아직 확인되지 않았습니다. 원래 작업은 보존됩니다. 나중에 “{resume}”로 다시 서명하지 않고 재시도하세요.'],
 ar:['لا يستطيع الدليل الوصول إلى التخزين حالياً. أعد المحاولة لاحقاً أو اختر دليلاً آخر.','لا يستطيع الدليل الوصول إلى التخزين حالياً، ولم يُؤكد النشر. تم الاحتفاظ بالمهمة الأصلية؛ استخدم «{resume}» لاحقاً لإعادة المحاولة دون توقيع جديد.'],
 ru:['Каталог временно не может обратиться к хранилищу. Повторите попытку позже или выберите другой каталог.','Каталог временно не может обратиться к хранилищу. Публикация не подтверждена. Исходная задача сохранена; позже используйте «{resume}» для повтора без новой подписи.'],
 de:['Das Verzeichnis kann derzeit nicht auf seinen Speicher zugreifen. Später erneut versuchen oder ein anderes Verzeichnis wählen.','Das Verzeichnis kann derzeit nicht auf seinen Speicher zugreifen. Die Veröffentlichung ist nicht bestätigt. Der ursprüngliche Auftrag bleibt gespeichert; später mit „{resume}“ ohne erneute Signatur wiederholen.'],
 es:['El directorio no puede acceder a su almacenamiento ahora. Reintente más tarde o elija otro directorio.','El directorio no puede acceder a su almacenamiento ahora. La publicación no está confirmada. La tarea original se conserva; use «{resume}» más tarde para reintentar sin volver a firmar.'],
 fr:['Le répertoire ne peut pas accéder à son stockage pour le moment. Réessayez plus tard ou choisissez un autre répertoire.','Le répertoire ne peut pas accéder à son stockage pour le moment. La publication n’est pas confirmée. La tâche d’origine est conservée ; utilisez « {resume} » plus tard pour réessayer sans signer à nouveau.'],
 it:['La directory non può accedere al proprio archivio al momento. Riprova più tardi o scegli un’altra directory.','La directory non può accedere al proprio archivio al momento. La pubblicazione non è confermata. L’attività originale è conservata; usa «{resume}» più tardi per riprovare senza firmare di nuovo.'],
};
export function trackStorageErrorText(locale,code,{publication=false,resume}={}){
 if(code!=='TRACK_DIRECTORY_STORAGE_UNAVAILABLE')return null;
 const values=labels[locale];if(!values||values.length!==2)throw Error('Incomplete storage error translations');
 if(!publication)return values[0];
 if(typeof resume!=='string'||!resume)throw Error('Publication resume label required');
 return values[1].replace('{resume}',resume);
}
