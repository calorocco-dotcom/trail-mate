# Website localization

The website supports English, Simplified Chinese, Taiwan Traditional Chinese,
Japanese, Korean, Arabic, Russian, German, Spanish, French and Italian.

Website language, firmware interface language and downloadable device language
packs are separate concepts. Selecting a website language does not install a pack
or change text rendered by the firmware inside the simulator.

## Directory contract

- `registry.js`: the supported IDs, native language names and writing direction.
  Both the page and component language selectors use this registry.
- `<locale>.js`: complete page, status, accessibility and package-description
  catalogs. Every language has the same semantic keys and parameter names.
- `features-<locale>.js`: independent names, summaries, descriptions and hardware
  requirements for each feature, including English. Stable IDs and source links
  remain in `../explorer-data.js`.
- `runtime.js`: lazy loading, active locale, aliases, interpolation and change
  notifications. Missing translations throw instead of silently showing English.
  The last requested language wins during asynchronous loading.
- `dom.js`: translates `data-i18n`, `data-i18n-aria`, `data-i18n-alt` and
  `data-i18n-placeholder`. Catalogs contain plain text, never executable markup.
- `component-labels.js`: connects the compiled React bundle to the same external
  runtime instance used by the page. Do not bundle a second language store.
- `pack-presentation.js`: formats device-pack names, descriptions, statuses,
  counts and memory profiles. `Intl.DisplayNames` localizes standardized language
  names, and `Intl.NumberFormat` formats counts. Prose is manually authored in
  the catalogs; English release metadata is not used as a translation fallback.
- `check-catalogs.mjs`: validates catalog keys, values and interpolation markers.

`../main.js` reads and updates the URL's `lang` parameter. A URL without an
explicit selection starts in English. The runtime
sets the document's `lang` and `dir`; Arabic uses RTL while SVG device geometry
remains LTR. Arabic formulas use Unicode directional isolation.

The device and track maintenance pages connect through the external
`management-runtime.js` bridge and `management-language.jsx`. They share the
same runtime, preserve `lang` on navigation, and switch without remounting the
business UI. `management-labels.js` currently covers configuration categories
and common controls in all eleven languages. The remaining maintenance prose,
field schemas, KML editors and Geocaching integration are still being migrated;
the presence of a language selector is not evidence of complete localization.
`tests/site/i18n-management.test.mjs` checks that additional catalog and runtime,
and `tests/site/i18n-management.browser.mjs` checks built pages and retained input.
`track-task-labels.js` covers the task status component in all eleven languages.
`management-copy.jsx` distributes the shared locale through React context without
giving each field its own runtime subscription. Task notices retain semantic keys,
not rendered translations. `tests/site/track-task-status.browser.mjs` checks live
locale changes while a simulated serial query is pending and after cancellation.
`track-history-labels.js` covers the history panel, including file operations,
confirmation and error states. Its keys and interpolation markers are checked by
the management locale test. Import/delete errors expose stable codes so the UI
does not match language-specific exception strings.
`track-preparation-labels.js` covers signature preparation and progress. The
preparation browser test checks pending-request language switches, retained IDs,
pause/resume and late replies. Preparation is never labelled as publication.
`device-record-labels.js` covers recording catalog, snapshot import and cleanup.
`track-device-catalog.browser.mjs` checks the built page with simulated USB frames,
including switches while connected, awaiting snapshot data and awaiting cleanup.
File names remain unchanged; progress and exact BigInt byte counts use Intl.
`track-directory-labels.js` covers directory discovery and selection. The USB
browser test checks retained address/selection while switching all locales and
while a directory reply is pending, as well as translated validation failures.
Directory readiness and cryptographic address checks are independent of locale.
`config-backup-labels.js` covers the configuration backup dialog and errors.
The dialog includes the shared language control because background controls are
inert while it is modal. The device browser test verifies password, processing
and source-review switches without extra device commands or configuration saves.
`geocaching-browse-labels.js` covers the browse shell, filters, counts and selection
labels. Geocaching reads the same URL/runtime locale and preserves worker, map,
selection and query state during switching. Dynamic service/detail prose and
maintenance forms still require migration from the earlier bilingual strings.
`geocaching-status-labels.js` and `geocaching/src/localized-status.js` retain
semantic presentation descriptors for dynamic statuses. Locale refresh formats
original counts and preserves diagnostic suffixes. Detail labels update in place,
preserving the open hint and original authored text. Transport diagnostics still
retain their original text; maintenance/publication forms remain in migration.
`geocaching-draft-labels.js` covers draft fields, options and validation. Draft
error codes are stable across locales; numeric rating labels are localized while
their wire values remain unchanged. The maintenance browser test checks switches
with invalid input, unsaved valid edits and an unconfirmed locked save. The outer
maintenance and publication controls still require migration.
`geocaching-publication-labels.js` now covers publication review, retained task
states and local stop controls. Its dialog includes the shared language selector.
The maintenance browser test checks unchanged checkpoint bytes, identities,
content and request/submit/stop counts across all locales, plus narrow Arabic and
German review layouts. Outer maintenance controls and raw transport diagnostics
remain in migration.

## Adding or revising a language

1. Add its BCP 47 ID, native name and direction to `registry.js`.
2. Create `<locale>.js` with all keys from `en.js`, and
   `features-<locale>.js` with all feature IDs and fields from `features-en.js`.
3. Translate complete sentences in natural regional usage. Preserve product
   names, URLs, hardware IDs and printed device legends. Taiwan Traditional
   Chinese requires its own terminology, not automatic character conversion.
4. Preserve placeholders such as `{tag}` and `{count}`. Use complete count
   labels instead of joining English singular/plural suffixes to a number.
5. New published device packs need a translated description in every page catalog
   and an entry in `describedPacks` in `pack-presentation.js`. Preserve input,
   review and hardware limitations; do not imply an unavailable capability.
6. Rebuild components after changing the registry or component source, and run
   the checks below.
7. Check the actual dropdown, a reload, feature search, package cards, errors and
   long text in the browser. For RTL, check mixed-script text and switch back to
   an LTR language. Automated completeness tests do not judge translation quality.

Do not use translation APIs or character-conversion tools as a substitute for
writing and reviewing the translations.

## Checks

From the repository root:

```sh
node site/i18n/check-catalogs.mjs
node --test tests/site/i18n-*.test.mjs
```

Tests cover all registered catalogs, actual lazy loading, feature coverage,
parameter parity, document direction, rapid switching, translated markup
attributes and the published device-pack catalog. The Pages workflow runs these
checks before publishing.

Browser verification of the current change covered all eleven selections at a
1280 px viewport, localized pack content and feature search, RTL-to-LTR switching,
URL persistence, and absence of page-level horizontal overflow. SVG geometry is
also checked at five available widths by `device-geometry.test.mjs`.
