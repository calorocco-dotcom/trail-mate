import React,{createContext,useContext,useMemo} from 'react';
import {trackTaskText} from './track-task-labels.js';
import {managementText} from './management-labels.js';
import {trackHistoryText} from './track-history-labels.js';
import {trackPreparationText} from './track-preparation-labels.js';
import {deviceRecordText} from './device-record-labels.js';
import {trackDirectoryText} from './track-directory-labels.js';
import {configBackupText} from './config-backup-labels.js';
import {kmlWorkspaceText} from './kml-workspace-labels.js';
import {kmlActionText} from './kml-action-labels.js';
import {geocachingBrowseText} from './geocaching-browse-labels.js';
import {geocachingDraftText} from './geocaching-draft-labels.js';
import {geocachingPublicationText} from './geocaching-publication-labels.js';
import {geocachingMaintenanceText} from './geocaching-maintenance-labels.js';
const LocaleContext=createContext('en');
export function useManagementLocale(){return useContext(LocaleContext);}
export function ManagementCopyProvider({locale,children}){return <LocaleContext.Provider value={locale}>{children}</LocaleContext.Provider>;}
export function useTrackTaskText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>trackTaskText(locale),[locale])};}
export function useManagementText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>managementText(locale),[locale])};}
export function useTrackHistoryText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>trackHistoryText(locale),[locale])};}
export function useTrackPreparationText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>trackPreparationText(locale),[locale])};}
export function useDeviceRecordText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>deviceRecordText(locale),[locale])};}
export function useTrackDirectoryText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>trackDirectoryText(locale),[locale])};}
export function useConfigBackupText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>configBackupText(locale),[locale])};}
export function useKmlWorkspaceText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>kmlWorkspaceText(locale),[locale])};}
export function useKmlActionText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>kmlActionText(locale),[locale])};}
export function useGeocachingBrowseText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>geocachingBrowseText(locale),[locale])};}
export function useGeocachingDraftText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>geocachingDraftText(locale),[locale])};}
export function useGeocachingPublicationText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>geocachingPublicationText(locale),[locale])};}
export function useGeocachingMaintenanceText(){const locale=useContext(LocaleContext);return {locale,text:useMemo(()=>geocachingMaintenanceText(locale),[locale])};}
