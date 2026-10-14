const labels={
 en:'This draft has an unfinished publication. Resume it, or discard its unsigned review, before saving changes or deleting the draft.',
 'zh-Hans':'此草稿有未完成的发布。请先继续原发布，或取消尚未签名的预览，再保存修改或删除草稿。',
 'zh-Hant-TW':'此草稿有未完成的發布。請先繼續原發布，或取消尚未簽署的預覽，再儲存修改或刪除草稿。',
 ja:'この下書きには未完了の公開があります。元の公開を再開するか、未署名のプレビューを破棄してから編集を保存または下書きを削除してください。',
 ko:'이 초안에 완료되지 않은 게시가 있습니다. 게시를 재개하거나 서명 전 미리보기를 취소한 뒤 변경 사항을 저장하거나 초안을 삭제하세요.',
 ar:'لهذه المسودة عملية نشر غير مكتملة. استأنفها أو ألغِ المعاينة غير الموقّعة قبل حفظ التغييرات أو حذف المسودة.',
 ru:'У черновика есть незавершённая публикация. Продолжите её или отмените неподписанный просмотр перед сохранением изменений или удалением.',
 de:'Dieser Entwurf hat eine unvollständige Veröffentlichung. Fortsetzen oder die unsignierte Vorschau verwerfen, bevor Änderungen gespeichert oder der Entwurf gelöscht wird.',
 es:'Este borrador tiene una publicación pendiente. Reanúdela o descarte la vista previa sin firmar antes de guardar cambios o eliminar el borrador.',
 fr:'Ce brouillon a une publication inachevée. Reprenez-la ou annulez l’aperçu non signé avant d’enregistrer des modifications ou de supprimer le brouillon.',
 it:'Questa bozza ha una pubblicazione incompleta. Riprendila o annulla l’anteprima non firmata prima di salvare modifiche o eliminare la bozza.',
};
export function trackDraftLockText(locale){if(!labels[locale])throw Error('Incomplete track draft lock translation');return labels[locale];}
