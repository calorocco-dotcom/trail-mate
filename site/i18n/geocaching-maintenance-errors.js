export const geocachingMaintenanceErrorKeys=['firmwareUnsupported','recoveryAuthorChanged','operationFailed','waitingPaused'];
export const geocachingMaintenanceErrorLabels={
 en:['Firmware does not support draft maintenance reads.','Device author changed during recovery.','The operation failed. See the diagnostic below.','Waiting paused.'],
 'zh-Hans':['固件尚不支持草稿维护读取。','恢复过程中设备作者已变化。','操作失败，请查看诊断信息。','已暂停等待。'],
 'zh-Hant-TW':['韌體尚不支援草稿維護讀取。','恢復過程中裝置作者已變更。','操作失敗，請查看診斷資訊。','已暫停等待。'],
 ja:['ファームウェアは下書き管理の読み取りに対応していません。','復元中にデバイスの作成者が変更されました。','操作に失敗しました。診断情報を確認してください。','待機を停止しました。'],
 ko:['펌웨어가 초안 관리 읽기를 지원하지 않습니다.','복구 중 기기 작성자가 변경되었습니다.','작업에 실패했습니다. 진단 정보를 확인하세요.','대기를 일시 중지했습니다.'],
 ar:['لا يدعم البرنامج الثابت قراءة المسودات لإدارتها.','تغيّر مؤلف الجهاز أثناء الاستعادة.','فشلت العملية. راجع معلومات التشخيص.','تم إيقاف الانتظار مؤقتاً.'],
 ru:['Прошивка не поддерживает чтение черновиков для управления.','Автор устройства изменился при восстановлении.','Операция не выполнена. См. диагностическую информацию.','Ожидание приостановлено.'],
 de:['Die Firmware unterstützt das Lesen zur Entwurfsverwaltung nicht.','Geräteautor während der Wiederherstellung geändert.','Vorgang fehlgeschlagen. Diagnoseinformationen beachten.','Warten pausiert.'],
 es:['El firmware no admite la lectura para mantener borradores.','El autor del dispositivo cambió durante la recuperación.','La operación falló. Consulta el diagnóstico.','Espera pausada.'],
 fr:['Le micrologiciel ne permet pas la lecture pour gérer les brouillons.','L’auteur de l’appareil a changé pendant la récupération.','L’opération a échoué. Consultez le diagnostic.','Attente suspendue.'],
 it:['Il firmware non supporta la lettura per gestire le bozze.','L’autore del dispositivo è cambiato durante il ripristino.','Operazione non riuscita. Consulta la diagnostica.','Attesa sospesa.'],
};
export function geocachingMaintenanceErrorText(locale){const values=geocachingMaintenanceErrorLabels[locale];if(!values||values.length!==geocachingMaintenanceErrorKeys.length)throw Error(`Incomplete geocaching maintenance error catalog: ${locale}`);return Object.fromEntries(geocachingMaintenanceErrorKeys.map((key,index)=>[key,values[index]]));}
export function geocachingMaintenanceFailure(error){
 const known={GEO_FIRMWARE_UNSUPPORTED:'firmwareUnsupported',GEO_RECOVERY_AUTHOR_CHANGED:'recoveryAuthorChanged'};
 const key=Object.hasOwn(known,error?.code)?known[error.code]:Object.hasOwn(known,error?.message)?known[error.message]:undefined;
 if(key)return {key};
 if(error?.name==='AbortError')return {key:'waitingPaused'};
 return {key:'operationFailed',detail:String(error?.message??error??'UNKNOWN_ERROR')};
}
