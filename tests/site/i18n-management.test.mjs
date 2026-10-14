import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {managementLabels,managementLabelKeys,managementText} from '../../site/i18n/management-labels.js';
import {trackTaskKeys,trackTaskLabels,trackTaskText} from '../../site/i18n/track-task-labels.js';
import {trackHistoryKeys,trackHistoryLabels,trackHistoryText} from '../../site/i18n/track-history-labels.js';
import {trackPreparationKeys,trackPreparationLabels,trackPreparationText} from '../../site/i18n/track-preparation-labels.js';
import {deviceRecordKeys,deviceRecordLabels,deviceRecordText} from '../../site/i18n/device-record-labels.js';
import {trackDirectoryKeys,trackDirectoryLabels,trackDirectoryText} from '../../site/i18n/track-directory-labels.js';
import {configBackupKeys,configBackupLabels,configBackupText} from '../../site/i18n/config-backup-labels.js';
import {kmlWorkspaceKeys,kmlWorkspaceLabels,kmlWorkspaceText} from '../../site/i18n/kml-workspace-labels.js';
import {kmlActionKeys,kmlActionLabels,kmlActionText} from '../../site/i18n/kml-action-labels.js';
import {geocachingBrowseKeys,geocachingBrowseLabels,geocachingBrowseText} from '../../site/i18n/geocaching-browse-labels.js';
import {geocachingDraftKeys,geocachingDraftLabels,geocachingDraftText} from '../../site/i18n/geocaching-draft-labels.js';
import {geocachingPublicationKeys,geocachingPublicationLabels,geocachingPublicationText} from '../../site/i18n/geocaching-publication-labels.js';
import {geocachingMaintenanceKeys,geocachingMaintenanceLabels,geocachingMaintenanceText} from '../../site/i18n/geocaching-maintenance-labels.js';
import {geocachingMaintenanceStatusKeys,geocachingMaintenanceStatusLabels,geocachingMaintenanceStatusText} from '../../site/i18n/geocaching-maintenance-status.js';
import {geocachingConnectionKeys,geocachingConnectionLabels,geocachingConnectionText} from '../../site/i18n/geocaching-connection-labels.js';
import {geocachingDraftActionKeys,geocachingDraftActionLabels,geocachingDraftActionText} from '../../site/i18n/geocaching-draft-action-labels.js';
import {geocachingTransferKeys,geocachingTransferLabels,geocachingTransferText} from '../../site/i18n/geocaching-transfer-labels.js';
import {geocachingMaintenanceErrorKeys,geocachingMaintenanceErrorLabels,geocachingMaintenanceErrorText,geocachingMaintenanceFailure} from '../../site/i18n/geocaching-maintenance-errors.js';
import {configInputKeys,configInputLabels,configInputText} from '../../site/i18n/config-input-labels.js';
import {configSchemaKeys,configSchemaLabels,configSchemaText} from '../../site/i18n/config-schema-labels.js';
import {configCollectionKeys,configCollectionLabels,configCollectionText} from '../../site/i18n/config-collection-labels.js';
import {deviceAuthorKeys,deviceAuthorLabels,deviceAuthorText} from '../../site/i18n/device-author-labels.js';
import {configActionKeys,configActionLabels,configActionText} from '../../site/i18n/config-action-labels.js';
import {configConnectionKeys,configConnectionLabels,configConnectionText,formatConfigConnectionNotice} from '../../site/i18n/config-connection-labels.js';
import {configTransferKeys,configTransferLabels,configTransferText} from '../../site/i18n/config-transfer-labels.js';
import {configImportKeys,configImportLabels,configImportText} from '../../site/i18n/config-import-labels.js';
import {configOnboardingKeys,configOnboardingLabels,configOnboardingText} from '../../site/i18n/config-onboarding-labels.js';
import {configNetworkFieldKeys,configNetworkFieldLabels,configNetworkFieldText} from '../../site/i18n/config-network-field-labels.js';
import {configCollectionIssueKeys,configCollectionIssueLabels,configCollectionIssueText,formatConfigCollectionIssue} from '../../site/i18n/config-collection-issue-labels.js';
import {configBasicFieldKeys,configBasicFieldLabels,configBasicFieldText} from '../../site/i18n/config-basic-field-labels.js';
import {getLocale,setLocale,translate} from '../../site/i18n/runtime.js';
import {initializeManagementLanguage,managementLanguageSnapshot,watchManagementLanguage,changeManagementLanguage,managementLanguageHref} from '../../site/i18n/management-runtime.js';

globalThis.location=new URL('https://example.test/trail-mate/tracks/?lang=zh-TW&task=abc');
globalThis.document={documentElement:{}};
globalThis.history={state:{draft:'keep'},replaceState(state,unused,href){assert.deepEqual(state,{draft:'keep'});globalThis.location=new URL(href);}};
await initializeManagementLanguage();
assert.equal(getLocale(),'zh-Hant-TW');
assert.equal(document.documentElement.lang,'zh-Hant-TW');
const snapshots=[];
const dispose=await watchManagementLanguage(value=>snapshots.push(value));
assert.deepEqual(Object.keys(managementLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(managementLabelKeys).size,managementLabelKeys.length);
assert.deepEqual(Object.keys(trackTaskLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(trackTaskKeys).size,trackTaskKeys.length);
assert.throws(()=>trackTaskText('invalid'),/Incomplete track task catalog/);
assert.deepEqual(Object.keys(trackHistoryLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(trackHistoryKeys).size,trackHistoryKeys.length);
assert.throws(()=>trackHistoryText('invalid'),/Incomplete track history catalog/);
assert.deepEqual(Object.keys(trackPreparationLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(trackPreparationKeys).size,trackPreparationKeys.length);
assert.throws(()=>trackPreparationText('invalid'),/Incomplete track preparation catalog/);
assert.deepEqual(Object.keys(deviceRecordLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(deviceRecordKeys).size,deviceRecordKeys.length);
assert.throws(()=>deviceRecordText('invalid'),/Incomplete device record catalog/);
assert.deepEqual(Object.keys(trackDirectoryLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(trackDirectoryKeys).size,trackDirectoryKeys.length);
assert.throws(()=>trackDirectoryText('invalid'),/Incomplete track directory catalog/);
assert.deepEqual(Object.keys(configBackupLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(configBackupKeys).size,configBackupKeys.length);
assert.throws(()=>configBackupText('invalid'),/Incomplete config backup catalog/);
assert.deepEqual(Object.keys(kmlWorkspaceLabels).sort(),locales.map(locale=>locale.id).sort());
assert.equal(new Set(kmlWorkspaceKeys).size,kmlWorkspaceKeys.length);
assert.throws(()=>kmlWorkspaceText('invalid'),/Incomplete KML workspace catalog/);
assert.deepEqual(Object.keys(kmlActionLabels).sort(),locales.map(locale=>locale.id).sort());assert.equal(new Set(kmlActionKeys).size,kmlActionKeys.length);
assert.throws(()=>kmlActionText('invalid'),/Incomplete KML action catalog/);
assert.deepEqual(Object.keys(geocachingBrowseLabels).sort(),locales.map(locale=>locale.id).sort());assert.equal(new Set(geocachingBrowseKeys).size,geocachingBrowseKeys.length);
assert.throws(()=>geocachingBrowseText('invalid'),/Incomplete geocaching browse catalog/);
assert.deepEqual(Object.keys(geocachingDraftLabels).sort(),locales.map(locale=>locale.id).sort());assert.equal(new Set(geocachingDraftKeys).size,geocachingDraftKeys.length);
assert.throws(()=>geocachingDraftText('invalid'),/Incomplete geocaching draft catalog/);
assert.deepEqual(Object.keys(geocachingPublicationLabels).sort(),locales.map(locale=>locale.id).sort());assert.equal(new Set(geocachingPublicationKeys).size,geocachingPublicationKeys.length);
assert.throws(()=>geocachingPublicationText('invalid'),/Incomplete geocaching publication catalog/);
assert.throws(()=>managementText('invalid'),/Incomplete management catalog/);
assert.deepEqual(Object.keys(geocachingMaintenanceLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(geocachingMaintenanceStatusLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(geocachingConnectionLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(geocachingDraftActionLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(geocachingTransferLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(geocachingMaintenanceErrorLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configInputLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configSchemaLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configCollectionLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(deviceAuthorLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configActionLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configConnectionLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configTransferLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configImportLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configOnboardingLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configNetworkFieldLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configCollectionIssueLabels).sort(),locales.map(locale=>locale.id).sort());
assert.deepEqual(Object.keys(configBasicFieldLabels).sort(),locales.map(locale=>locale.id).sort());
for(const locale of locales){const copy=configBasicFieldText(locale.id);assert.deepEqual(Object.keys(copy),configBasicFieldKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
for(const locale of locales){const copy=configCollectionIssueText(locale.id),english=configCollectionIssueText('en');assert.deepEqual(Object.keys(copy),configCollectionIssueKeys);for(const key of configCollectionIssueKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}assert.equal(formatConfigCollectionIssue({code:'COLLECTION_IFAC_INVALID'},locale.id),copy.COLLECTION_IFAC_INVALID);assert.ok(formatConfigCollectionIssue({code:'future',message:'$& unknown'},locale.id).includes('$& unknown'));}
for(const locale of locales){const copy=configNetworkFieldText(locale.id);assert.deepEqual(Object.keys(copy),configNetworkFieldKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
for(const locale of locales){const copy=configOnboardingText(locale.id);assert.deepEqual(Object.keys(copy),configOnboardingKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
for(const locale of locales){const copy=configImportText(locale.id),english=configImportText('en');assert.deepEqual(Object.keys(copy),configImportKeys);for(const key of configImportKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}assert.ok(formatConfigConnectionNotice({key:'importFailed',diagnosticKey:'backupTooLarge'},locale.id,copy).includes(copy.backupTooLarge));}
for(const locale of locales){const copy=configTransferText(locale.id),english=configTransferText('en');assert.deepEqual(Object.keys(copy),configTransferKeys);for(const key of configTransferKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}assert.ok(formatConfigConnectionNotice({key:'saveUnconfirmed',diagnostic:'$& literal'},locale.id,copy).includes('$& literal'));assert.ok(formatConfigConnectionNotice({key:'cancelUnconfirmed',diagnosticKey:'cancelReceiptInvalid'},locale.id,copy).includes(copy.cancelReceiptInvalid));}
for(const locale of locales){const copy=configConnectionText(locale.id),english=configConnectionText('en');assert.deepEqual(Object.keys(copy),configConnectionKeys);for(const key of configConnectionKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}assert.ok(formatConfigConnectionNotice({key:'reading',values:{offset:320,total:1200}},locale.id).includes(new Intl.NumberFormat(locale.id).format(1200)));assert.ok(formatConfigConnectionNotice({key:'readFailed',diagnostic:'$& <literal>'},locale.id).includes('$& <literal>'));assert.ok(formatConfigConnectionNotice({key:'connectionFailed',diagnosticKey:'capabilitiesInvalid'},locale.id).includes(copy.capabilitiesInvalid));}
for(const locale of locales){const copy=configActionText(locale.id),english=configActionText('en');assert.deepEqual(Object.keys(copy),configActionKeys);for(const key of configActionKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}}
for(const locale of locales){const copy=deviceAuthorText(locale.id),english=deviceAuthorText('en');assert.deepEqual(Object.keys(copy),deviceAuthorKeys);for(const key of deviceAuthorKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}}
for(const locale of locales){const copy=configCollectionText(locale.id),english=configCollectionText('en');assert.deepEqual(Object.keys(copy),configCollectionKeys);for(const key of configCollectionKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}}
for(const locale of locales){const copy=configSchemaText(locale.id);assert.deepEqual(Object.keys(copy),configSchemaKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
for(const locale of locales){const copy=configInputText(locale.id),english=configInputText('en');assert.deepEqual(Object.keys(copy),configInputKeys);for(const key of configInputKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}}
for(const locale of locales){const copy=geocachingMaintenanceErrorText(locale.id);assert.deepEqual(Object.keys(copy),geocachingMaintenanceErrorKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
assert.deepEqual(geocachingMaintenanceFailure(Error('GEO_FIRMWARE_UNSUPPORTED')),{key:'firmwareUnsupported'});
assert.deepEqual(geocachingMaintenanceFailure({code:'GEO_RECOVERY_AUTHOR_CHANGED'}),{key:'recoveryAuthorChanged'});
assert.deepEqual(geocachingMaintenanceFailure({name:'AbortError',message:'cancelled'}),{key:'waitingPaused'});
for(const message of ['unexpected wire receipt','toString','__proto__'])assert.deepEqual(geocachingMaintenanceFailure(Error(message)),{key:'operationFailed',detail:message});
for(const locale of locales){const copy=geocachingTransferText(locale.id),english=geocachingTransferText('en');assert.deepEqual(Object.keys(copy),geocachingTransferKeys);for(const key of geocachingTransferKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}}
for(const locale of locales){const copy=geocachingDraftActionText(locale.id);assert.deepEqual(Object.keys(copy),geocachingDraftActionKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
for(const locale of locales){const copy=geocachingConnectionText(locale.id);assert.deepEqual(Object.keys(copy),geocachingConnectionKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
for(const locale of locales){const copy=geocachingMaintenanceStatusText(locale.id);assert.deepEqual(Object.keys(copy),geocachingMaintenanceStatusKeys);assert.ok(Object.values(copy).every(value=>value.trim()));}
assert.equal(new Set(geocachingMaintenanceKeys).size,geocachingMaintenanceKeys.length);
for(const locale of locales){
 const copy=geocachingMaintenanceText(locale.id),english=geocachingMaintenanceText('en');
 assert.deepEqual(Object.keys(copy),geocachingMaintenanceKeys);
 for(const key of geocachingMaintenanceKeys){assert.ok(copy[key].trim());assert.deepEqual([...copy[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...english[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
}
for(const locale of locales){
 await changeManagementLanguage(locale.id);
 const snapshot=managementLanguageSnapshot();
 assert.equal(snapshot.locale,locale.id);
 assert.equal(snapshot.languageLabel,translate('common.language'));
 assert.ok(snapshot.deviceTitle.trim());assert.ok(snapshot.tracksTitle.trim());
 assert.deepEqual(Object.keys(snapshot.text),managementLabelKeys);
 assert.ok(Object.values(snapshot.text).every(value=>typeof value==='string'&&value.trim()));
 const taskText=trackTaskText(locale.id);
 assert.deepEqual(Object.keys(taskText),trackTaskKeys);
 assert.ok(Object.values(taskText).every(value=>typeof value==='string'&&value.trim()));
 const historyText=trackHistoryText(locale.id),englishHistory=trackHistoryText('en');
 assert.deepEqual(Object.keys(historyText),trackHistoryKeys);
 for(const key of trackHistoryKeys){assert.ok(historyText[key].trim());assert.deepEqual([...historyText[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...englishHistory[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
 const preparationText=trackPreparationText(locale.id),englishPreparation=trackPreparationText('en');
 assert.deepEqual(Object.keys(preparationText),trackPreparationKeys);
 for(const key of trackPreparationKeys){assert.ok(preparationText[key].trim());assert.deepEqual([...preparationText[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...englishPreparation[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
 const recordText=deviceRecordText(locale.id),englishRecord=deviceRecordText('en');
 assert.deepEqual(Object.keys(recordText),deviceRecordKeys);
 for(const key of deviceRecordKeys){assert.ok(recordText[key].trim());assert.deepEqual([...recordText[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...englishRecord[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
 const directoryText=trackDirectoryText(locale.id),englishDirectory=trackDirectoryText('en');
 assert.deepEqual(Object.keys(directoryText),trackDirectoryKeys);
 for(const key of trackDirectoryKeys){assert.ok(directoryText[key].trim());assert.deepEqual([...directoryText[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...englishDirectory[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
 const backupText=configBackupText(locale.id);assert.deepEqual(Object.keys(backupText),configBackupKeys);assert.ok(Object.values(backupText).every(value=>typeof value==='string'&&value.trim()));
 const workspaceText=kmlWorkspaceText(locale.id),englishWorkspace=kmlWorkspaceText('en');assert.deepEqual(Object.keys(workspaceText),kmlWorkspaceKeys);
 for(const key of kmlWorkspaceKeys){assert.ok(workspaceText[key].trim());assert.deepEqual([...workspaceText[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...englishWorkspace[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
 const actionText=kmlActionText(locale.id);assert.deepEqual(Object.keys(actionText),kmlActionKeys);assert.ok(Object.values(actionText).every(value=>typeof value==='string'&&value.trim()));
 const geoText=geocachingBrowseText(locale.id),englishGeo=geocachingBrowseText('en');assert.deepEqual(Object.keys(geoText),geocachingBrowseKeys);
 for(const key of geocachingBrowseKeys){assert.ok(geoText[key].trim());assert.deepEqual([...geoText[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...englishGeo[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
 const draftText=geocachingDraftText(locale.id),englishDraft=geocachingDraftText('en');assert.deepEqual(Object.keys(draftText),geocachingDraftKeys);
 for(const key of geocachingDraftKeys){assert.ok(draftText[key].trim());assert.deepEqual([...draftText[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort(),[...englishDraft[key].matchAll(/\{(\w+)\}/g)].map(match=>match[1]).sort());}
 const publicationText=geocachingPublicationText(locale.id);assert.deepEqual(Object.keys(publicationText),geocachingPublicationKeys);assert.ok(Object.values(publicationText).every(value=>typeof value==='string'&&value.trim()));
 assert.equal(location.searchParams.get('lang'),locale.id);
 assert.equal(location.searchParams.get('task'),'abc');
 assert.equal(document.documentElement.dir,locale.direction);
 const internal=new URL(managementLanguageHref('../device/?draft=123#editor'));
 assert.equal(internal.searchParams.get('lang'),locale.id);
 assert.equal(internal.searchParams.get('draft'),'123');assert.equal(internal.hash,'#editor');
 assert.equal(managementLanguageHref('https://outside.test/kml?id=9'),'https://outside.test/kml?id=9');
}
// Both the management bridge and existing components see the same store.
await setLocale('ar');assert.equal(snapshots.at(-1).locale,'ar');
await Promise.all([changeManagementLanguage('de'),changeManagementLanguage('ja')]);
assert.equal(getLocale(),'ja');assert.equal(location.searchParams.get('lang'),'ja');
const count=snapshots.length;dispose();await setLocale('en');assert.equal(snapshots.length,count);
console.log('Management locales: all 11 titles, shared runtime, aliases, URL preservation, directions and rapid-switch ordering passed.');
