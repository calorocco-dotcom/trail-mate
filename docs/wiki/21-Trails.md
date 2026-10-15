# Trails and decentralized sharing

Trail Mate combines local device track recording, KML route use and a browser
workspace for maintaining and sharing trails over Reticulum. The browser uses
your connected device as its author identity. No website account is needed.

This guide describes the 1.0.0 candidate. Final device acceptance and the public
Tracks page are still pending. The public directory supplies a global two-level
catalog with 251 country/region parents and 4,596 administrative units. Refer to the
Releases page for available firmware rather than treating this guide as a
release announcement.

## Your trail library

Open the Tracks workspace and import a KML/KMZ file or a supported GPX/CSV/BIN
device record. Connect USB when reading records directly from the device. The
workspace distinguishes your browser drafts from records stored on the device
and trails already published by an author.

Drafts remain in the current browser until you publish or export them. Clearing
browser data can remove local drafts; export a backup before moving to another
browser or computer. Importing a shared trail as an editable copy does not give
you ownership of its original publication.

## Inspect and maintain a trail

Use the map and item selectors to inspect segments and waypoints. The workspace
provides supported geometry and KML metadata editing, attachments, elevation
inspection and timed-record replay. Elevation and time are derived from the
actual file data; missing values are not invented. Preserve source files when
converting an imported recording to KML.

Save edits to the draft and review the resulting changes. Portable backups and
signed-package exports serve different purposes: a draft backup preserves
editable local work, while a signed package carries a publication's original
signature and content. The UI identifies unavailable optional attachments;
receiving a manifest does not imply every image has already been downloaded.

## Discover other people's trails

Connect through an available Reticulum WebSocket gateway and choose a verified
trail directory. The gateway transports packets; it does not have to host the
directory itself. You may use independent directories reachable through that
network.

Pan or zoom the map and search the visible bounds. Enable search after movement
to update results as you explore. A selected administrative region is a query
boundary; moving the map does not silently change that region. The directory
must supply a compatible region catalog. The intended selection stays simple:
country/region and first-level administration, globally rather than China-only.
The public catalog stops after these two levels. Names use source translations
for the 11 Web languages where available and otherwise use the region's base
name. Its simplified Natural Earth boundaries support approximate discovery;
the source's administrative classifications may differ from current national
subdivisions. Map-extent search remains useful near simplified boundaries.
Use **Load regions**, **More regions**, and **Select region** to browse
the catalog; select **Use map area** to return to map-based searching.

Open a result to inspect its author, version, route data and resources. Directory
summaries are discovery information; the detail workflow checks the signed
publication. Save content locally for offline use when supported.

## Share an ID or a link

Copy the trail publication ID or the version link from its publication/detail
view. A publication ID identifies a shared trail across updates; a version link
identifies a particular revision. A KML `Placemark` ID identifies an item inside
one file and is not a universal network address.

Recipients can use the copied publication ID in direct lookup, without knowing
your website account because no such account is required. Availability still
depends on a reachable directory that has the requested content.

## Publish, update and archive

1. Connect the author device and allow its identity proof to complete.
2. Review the draft, target directory and public metadata.
3. Authorize publication. The device signs; the browser sends the publication
   through the computer's network and verifies the directory result.
4. Let device confirmation finish before starting another publication. If it is
   incomplete, continue the retained task rather than discarding its identity.

Synchronization also cleans the exact temporary KML, manifest, receipt and
attachment uploads after the device acknowledgement has been retained. Keep
the browser draft and signing bindings until this finishes. Cleanup removes
temporary transfer files, not the stored route package or published content.

Updating a confirmed draft keeps its publication ID and references the previous
signed version. Archiving publishes an archive state; it does not erase copies
held by recipients. Only your author identity can maintain its publication.
Publishing someone else's editable copy creates a separate publication under
your identity rather than changing theirs.

## Device use

Tracker recording and route modes remain usable on supported devices. Installed
routes can provide map overlays, elevation previews and supported waypoint
images. Package installation depends on firmware capabilities, storage and the
resources included in the package. A successful browser preview is not proof
that every KML presentation feature can render on every embedded target.

**Tracker → Menu → Nav** adds locally calculated origin-to-destination foot routes
on supported targets with routing data. It reuses the existing POI picker and
saves KML plans separately from recordings and imported routes. See
[[24. Offline Navigation and Map Controls]] for the complete device workflow.

See the existing Offline Maps and Trail Mate Center guides for preparing map
coverage around a route. See Device Identity and Sharing Privacy before
publishing routes containing precise positions or recording times.
