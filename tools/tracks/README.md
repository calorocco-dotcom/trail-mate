# Native track directories

`directory_service.py` runs the production `trailmate.track.v1` service with an
explicit existing Reticulum configuration. Device author signatures remain on
the original manifests; the service's transport identity is separate.

```sh
python -m tools.tracks.directory_service --state /private/tracks --rns-config /private/reticulum --replica-seed <track-discovery-hash>
```

Use at most three 32-hex **track discovery** hashes. Do not use LXMF delivery or
Geocaching discovery hashes. Signed announcements and an extended capability
exchange still bind the discovered directory's public key and epoch. A seed
requests discovery; it grants no trust to content. The handler accepts signed
path responses so a new service can find directories that started earlier.

## Global country/region and administrative selection

`data/country-regions.json` adapts the 242 country/region boundaries already
shipped with the Geocaching page. It adds no province/city/county database.
The directory serves these as one selectable level through its existing region
operations; compatible operator catalogs may provide additional levels.
This dataset does not claim complete worldwide first-level administration.

`data/world-regions.json` adds one administrative selection level using pinned
Natural Earth Admin 1 data: 251 country/region parents and 4,596 administrative
units. It retains the existing 242 country boundaries and adds nine parents
needed by the administrative source. The file is 7,963,790 bytes, within the
existing 8 MiB reader limit. Use it with the same directory protocol:

```sh
python -m tools.tracks.directory_service --state /private/tracks --rns-config /private/reticulum --regions-file tools/tracks/data/world-regions.json --regions-namespace world
```

The global hierarchy has only country/region and one administrative level.
It adds no city/county database, country-specific hierarchy or firmware data.
Native names are provided for the 11 Web locales where the source supplies them;
missing names use the region's base name. The first-level polygons use
topology-preserving 0.025-degree simplification and five-decimal coordinates.
This is approximate discovery geometry, not a current legal boundary service.
The source may classify departments, territories and unnamed areas as Admin 1;
its administrative units can differ from current national subdivisions.
Map-extent search remains available near simplified boundaries.

Natural Earth is public domain. Source documentation:
[Admin 1](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-1-states-provinces/)
and [terms](https://www.naturalearthdata.com/about/terms-of-use/).
The generator pins revision `ca96624a56bd078437bca8184e78163e5039ad19` and verifies
both input SHA-256 values. Download the two GeoJSON inputs from that revision's
`geojson/` directory and run with build-time Shapely 2.1.2 and the existing
directory Python dependencies installed:

```sh
python -m tools.tracks.build_world_regions --countries-source ne_10m_admin_0_countries.geojson --admin-source ne_10m_admin_1_states_provinces.geojson --output tools/tracks/data/world-regions.json
```

Generation uses the existing country catalog and Node locale registry. Runtime
loading requires neither Shapely nor Node. Regenerating administrative data
changes the catalog version, so connected browsers must refresh their selected
directory catalog. Directory identity, published routes and author keys remain
in the existing service state.

```sh
python -m tools.tracks.directory_service --state /private/tracks --rns-config /private/reticulum --regions-file tools/tracks/data/country-regions.json --regions-namespace world
```

The source and public-domain terms are documented in
`site/geocaching/data/README.md`. Country labels are generated for all 11 Web
locales using Node's `Intl.DisplayNames`. Generalized boundaries have the same
coastal/small-island limits as the source; map-extent search remains available.
The polar region is divided into bounded polygon pieces without omitting it.

To regenerate, install build-time Shapely 2.1.2 and run:

```sh
python -m tools.tracks.build_country_regions --output tools/tracks/data/country-regions.json
```

Node and Shapely are needed only for generation. The running directory uses the
existing JSON catalog reader; neither the dataset nor a new dependency is sent
to firmware or allocated in device RAM.

`TRAIL_MATE_TRACK_REPLICA_SEEDS` supplies comma-separated defaults when no
`--replica-seed` argument is passed. The shared NAS Compose template maps
`TRACK_REPLICA_SEEDS` from `.env`, including with the administrative-catalogue
override. Service identity, content, upload offsets and peer progress persist in
the track data volume. No browser login, device RF mode or USB transport is used
for directory-to-directory replication.

Replication runs automatically for compatible discovered peers. The loop queues
at most 32 announcements, stores at most 32 peers, and allows one active replica
request. Pages contain at most 16 references; each file request carries at most
1024 bytes. A round has a 256 KiB wire budget, then yields. Completed rounds poll
for changes every 300 seconds; failures back off 60/300/1800/21600 seconds.
These are explicit bounds, not measured NAS throughput or capacity guarantees.

The receiver freezes an inventory high watermark, obtains exact original
manifests and verifies every inventory binding. Missing parents can be fetched
with a 64-ancestor bound. It copies the exact KML and signed attachment index,
then required resources and available optional resources. File hashes and
package bindings are checked before version import. A page checkpoint advances
only after every referenced version and its required files have committed.
Duplicate versions must still pass physical content checks; missing files are
refilled before progress is claimed. Valid signature forks propagate as
evidence, without declaring either branch the current winner.

Before each request, exact request bytes and ID are persisted. File offsets
commit first, so a crash between a chunk write and peer-state update can retry
the original range. Replica-only temporary-file expiry extends on newly written
bytes; repeated old ranges do not renew it. Ordinary browser upload lifetime is
unchanged. Peer epoch changes reset the checkpoint and cancel only that peer's
unfinished local temporary upload, preserving immutable files and versions.

By default, automatic file copies wait for a reported next-hop bitrate of at
least 100,000 bit/s. Unknown paths are requested and rechecked separately from
known slow paths, including after restart. The bitrate is Reticulum's interface
estimate (for example a TCP guess), not a bandwidth measurement; shared instances
may expose the local shared interface rather than the complete underlying path.
Keep the NAS transport on the intended IP interfaces. The operator may explicitly
enable slow/unknown paths with `--allow-low-bandwidth-replication`; this can create
large background traffic and is not enabled by the Compose template.

`TRAIL_MATE_REPLICATION_DEBUG=1` adds public request IDs/operations to logs for
diagnostics. It does not print author private keys, file bodies or upload tokens.
Normal logs report discovery, completed pages and failures.

## Local verification

```sh
python -m unittest tools.tracks.test_replication_inventory tools.tracks.test_replication
python -m unittest tools.tracks.test_storage_recovery
python -m unittest tools.tracks.test_storage_network
python -m unittest tools.tracks.test_replication_network
```

The network test requires website dependencies, `npm run build:tracks` from
`site/`, and a Playwright browser. It runs
two real native directories over loopback, interrupts a partial file copy,
restarts the replica with its persistent state, and stops the source. Only after
the source TCP listener is confirmed offline does a fresh browser read the
archived v2 package, original author-signed history, exact KML, index and PNG from
the replica. v1 and v2 histories and a v2 author fork are copied. The built track
page then reads both original signatures, selects a historical version, verifies
its required package and saves an editable copy. After disconnect and reload,
the test independently checks the persistent draft and original author proof.
The production history reader also verifies the selected archive's full signature
path (3→2→1) and the built page opens an exact historical version. The selected
fork branch's path is checked separately; this does not claim the other branch
or globally unique currentness. History reads use metadata operation 2 only and
stop at a 64-version budget with an explicit incomplete state when needed.
No USB, RF or public publication is involved. Windows testing owns the actual interpreter PID instead of a venv
redirector so stopping a source really removes its listener.

This local test does not verify public deployment, Linux container permissions,
maximum file/package throughput, many-peer fairness, power-loss behavior, or
global fork resolution. Retention remains bounded by disk/file/
version admission; a directory can refuse additional copies. Data should not be
considered globally replicated merely because one node issued a publish receipt.

Valid requests encountering SQLite or file I/O failures return an authenticated
503 `storage unavailable` with the original request ID, without a success receipt
or private diagnostic text. Background replication waits 30 seconds after a
storage exception; cleanup uses an independent 60-second guard. Durable requests,
file offsets and checkpoints remain the recovery authority. The storage tests use
actual SQLite readonly/locking/full/failed-commit conditions, controlled transport
and clocks, plus an injected fsync error; they do not prove physical power-loss
recovery. Where database registration cannot be established, uncertain temporary
files are retained rather than deleted speculatively.

The storage-network test runs a real local directory, gateway and browser against
an isolated SQLite connection set to readonly, then restores writes through a
test-only marker. It verifies authenticated 503 classification, capabilities
while writes fail, no empty upload accumulation, fixed-ID upload recovery and
exact seal/read without changing directory identity or epoch. The fault seam is
not present in the production service. Browser publication UI tests separately
verify retained unconfirmed tasks and the same signed content/request ID across
storage failure, reload, lost receipt and successful confirmation in all eleven
locales; their USB/network boundaries are controlled.
