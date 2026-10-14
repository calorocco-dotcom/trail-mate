# Synology DDNS deployment on TCP 18434

Use `vicliu.i234.me` with public TCP **18434** forwarded to NAS TCP **18434**. This matches the GitHub Pages endpoint. No Cloudflare account, tunnel token or NAS ports 80/443 are needed. A dynamic public IP is supported through DDNS; the router must have a reachable public IP and allow port forwarding.

```text
Browser -> wss://vicliu.i234.me:18434/
        -> router TCP 18434 -> NAS TCP 18434
        -> bridge TLS listener, container port 8787
        -> loopback Reticulum TCP 44242 + persistent directory
```

Three containers use the same locally built Python image. `directory` owns the shared Reticulum instance, `tracks` joins it with a separate persistent service identity and data volume, and `bridge` exposes the packet gateway. Bridge and tracks share directory's private network namespace, so the single published TLS port is declared on `directory`. Port 44242 remains loopback-only and unpublished. No container uses host networking or the Docker socket. Cache and track queries are live.

The packet bridge paces valid bursts with backpressure rather than closing a healthy file transfer. `BRIDGE_PACKET_RATE` defaults to 512 raw packets/s per client and `BRIDGE_BURST_PACKETS` to 128; each accepts 1–4096. Lower rates can be used for a constrained upstream. The 500-byte MTU, 16-packet WebSocket queue and 32-client deployment limit remain bounded. Track directories use a 10 ms reply cadence while replies are active, with a 100 ms idle wait; Geocaching keeps its existing reply cadence. These settings concern computer services and do not change firmware RAM or RF configuration.

## Requirements

- Linux Docker Engine and Docker Compose v2.17 or later.
- An amd64 or arm64 NAS able to pull the Python image and install pinned Python packages.
- Working DDNS and a router forwarding TCP 18434 to a reserved/stable NAS LAN address on port 18434.
- A valid browser-trusted certificate covering `vicliu.i234.me`, its full chain and matching private key.
- Outbound DNS, HTTPS for installation, TCP 4242 for bootstrap peers and the TCP ports advertised by automatically discovered peers.

A certificate covers the hostname, not the port. Use the appropriate DSM-managed certificate or establish issuance/renewal supported by your DSM setup. Compose does not issue certificates and does not assume HTTP-01 can use blocked port 80. Do not use a self-signed certificate or disable browser verification.

## Get the files

```sh
git clone https://github.com/vicliu624/trail-mate.git
cd trail-mate/tools/geocaching/deploy
cp .env.example .env
```

Use a published revision containing both directory services; the current local feature branch has not been published. For an existing checkout, preserve local changes and update the intended revision. The complete repository is required because the Dockerfile uses both service source trees. Its ignore file excludes certificates, databases, test fixtures and firmware output from the build context.

### Updating Reticulum discovery on an existing deployment

Merge the discovery settings from `reticulum/config` into the configuration
mounted by your deployment. The template enables native discovery, requires a
stamp value of 16, allows two automatically connected interfaces, and marks the
three public seeds as `bootstrap_only`. Preserve custom peers and private access
credentials. Reticulum retains discovery state in the existing `reticulum-data`
volume; do not remove that volume during an update.

After updating the mounted configuration, recreate both services together because
the bridge and tracks share the directory container's network namespace:

```sh
docker compose up -d --force-recreate directory tracks bridge
docker compose exec directory rnstatus --config /var/lib/trail-mate/reticulum -d
docker compose exec directory rnstatus --config /var/lib/trail-mate/reticulum -a
```

The first status command lists discovered candidates; the second shows connected
interfaces. A listening WSS port alone does not prove upstream connectivity.
Bootstrap interfaces can overlap with discovered connections while Reticulum
reaches its target; this template does not impose the device's single-socket
policy on the NAS. If every cached and bootstrap endpoint is unreachable, native
discovery cannot learn new endpoints until one connection becomes available.

Configure `.env`:

```dotenv
WSS_HOST=vicliu.i234.me
WSS_PORT=18434
TLS_CERT_DIR=./certs
DIRECTORY_NAME=Trail Mate public directory
TRACK_DIRECTORY_NAME=Trail Mate public tracks
TRACK_REPLICA_SEEDS=
SITE_ORIGIN=https://vicliu624.github.io
```

Origin must not include the website path. Each directory name is limited to 40 UTF-8 bytes. `WSS_HOST` controls certificate health verification; DDNS and the website endpoint are configured separately. Remove the unused `TUNNEL_TOKEN` from an earlier deployment's `.env`.

`TRACK_REPLICA_SEEDS` optionally lists up to three comma-separated **track discovery** hashes from other compatible directories. Track services automatically pull signed histories and complete file packages from discovered peers, with persistent progress and bounded rounds. This setting also works with the administrative override. It is independent of the website's `trackDiscoverySeeds`, which tells browser visitors which service to discover. See `tools/tracks/README.md` for budgets, restart handling and verification boundaries. The default bandwidth guard uses Reticulum interface estimates; do not treat it as a measured end-to-end throughput guarantee.

## Optional administrative hierarchy for tracks

Without an administrative catalogue, tracks still support map-bound queries and direct ID reads; administrative-region operations report unsupported. To enable them, provide a real, licensed and versioned GeoJSON FeatureCollection accepted by `tools.tracks.region_catalog.RegionCatalog`. Each Polygon/MultiPolygon Feature has a stable string `id`, `properties.name`, optional `properties.names` locale translations, and `properties.parent` referencing another Feature ID or null. The service derives hierarchy levels and a boundary version hash. Do not substitute rectangular test regions for actual administrative boundaries.

Set `TRACK_REGIONS_FILE` to an existing absolute file path and `TRACK_REGIONS_NAMESPACE` to a stable identifier matching `[a-z][a-z0-9.-]{0,23}`. The file is mounted read-only, independently of track identity/data. Then use the override consistently:

```sh
docker compose -f compose.yaml -f compose.track-regions.yaml config --quiet
docker compose -f compose.yaml -f compose.track-regions.yaml up -d --no-build
```

The override refuses to create a missing file. Catalogue validation failure stops the track service instead of silently dropping administrative queries. This repository does not yet package a production global administrative catalogue; region search is not accepted on the public deployment until a real catalogue has been installed and verified.

## Prepare the certificate

Export or copy the correct DSM-managed certificate into a dedicated deployment directory:

```text
certs/
  fullchain.pem
  privkey.pem
```

`fullchain.pem` contains the leaf certificate followed by its intermediates. `privkey.pem` is its matching unencrypted private key. Inspect the actual DSM export rather than assuming its file names. Confirm the subject alternative names include `vicliu.i234.me`, expiry is valid, and certificate/private-key public keys match.

The image runs as UID/GID **10001:10001**. Set permissions on the deployment copy only:

```sh
sudo chown -R 10001:10001 ./certs
sudo chmod 700 ./certs
sudo chmod 600 ./certs/fullchain.pem ./certs/privkey.pem
```

Do not change DSM's system certificate-store ownership or mount all NAS keys. The deployment certificate directory is ignored by Git, excluded from the image and mounted read-only. `TLS_CERT_DIR` can instead name an absolute dedicated directory. The bind mount refuses to create a missing source directory.

## Router and firewall

Forward **TCP 18434 external -> NAS LAN IP:18434 internal** and allow it in the NAS firewall. No UDP forwarding is required. Do not forward 44242 or 8787 directly. Verify existing services are not using NAS TCP 18434.

For an existing installation, preserve `.env` and inspect `docker compose port directory 8787` before changing anything. `WSS_PORT` is the NAS-side port, not necessarily the public port. Public TCP 18434 forwarded to NAS TCP 18433 is also valid when the existing `.env` has `WSS_PORT=18433`. The public endpoint must match `site/geocaching/network.json`; internal and external ports need not be equal.

If an AAAA record exists, verify its IPv6 routing/firewall too; an unreachable IPv6 destination can disrupt access even when IPv4 works. Check DDNS after WAN address changes. Test externally because LAN NAT loopback behavior varies.

## Build and start

```sh
docker compose config --quiet
docker compose build directory
docker compose up -d --no-build --remove-orphans
docker compose ps
docker compose logs --tail=60 directory tracks bridge
```

Only Python services are built, not device firmware. `--remove-orphans` removes the previous `tunnel` container from this same Compose project, if present, without deleting named volumes. Check the project name before using it.

Both directories emit separate `ready` events containing `delivery`, **`discovery`**, and epoch; the Geocaching directory also reports sequence. Bridge emits `bridge_ready` with `tls: true`. All three processes restart after exit and their logs rotate at 5 MB with three retained files.

Directory health verifies its local TCP listener. Bridge health verifies a local TLS handshake using the real hostname, system trust roots and expiry checks. These do not prove public port forwarding or application queries. An unhealthy running container is not automatically restarted by Docker.

## External verification and handoff

Test `wss://vicliu.i234.me:18434/` from another Internet connection with Origin **`https://vicliu624.github.io`**. Other Origins are intentionally rejected. An ordinary HTTPS GET may be rejected because this is a WebSocket endpoint, not a homepage.

If TLS closes before the WebSocket handshake, collect these read-only diagnostics from the deployment directory:

```sh
docker compose ps
docker compose port directory 8787
docker compose logs --tail=80 bridge directory
```

Compare the reported NAS port with the router's forwarding destination. The bridge must report `bridge_ready` with `tls: true`; a healthy directory TCP listener alone does not establish that WSS is available. Do not send certificate keys or the contents of `.env`.

Return the WSS URL and each service's **discovery** hash from its ready log, service status and external connectivity results to the website operator. Never send the private key or database. Set `site/geocaching/network.json` as follows, using actual discovery hashes (not delivery hashes):

```json
{
  "endpoint": "wss://vicliu.i234.me:18434/",
  "discoverySeeds": ["<32 hexadecimal characters from directory ready.discovery>"],
  "trackDiscoverySeeds": ["<32 hexadecimal characters from tracks ready.discovery>"]
}
```

Each list allows at most three addresses. The protocols have different discovery destinations; never reuse a Geocaching hash as a track seed. Website visitors use the configured track seeds automatically unless explicitly overriding the optional input. The signed announcement and capability exchange still determine whether a discovered service is admitted. Connecting WSS alone is insufficient.

From a source checkout with website dependencies and a Playwright browser installed, run the explicit read-only acceptance tool against the formal page origin:

```sh
node tools/tracks/browser_network_probe.mjs wss://vicliu.i234.me:18434/ https://vicliu624.github.io/trail-mate/geocaching/ geocaching 75
node tools/tracks/browser_network_probe.mjs wss://vicliu.i234.me:18434/ https://vicliu624.github.io/trail-mate/geocaching/ tracks 75
```

Run these from the repository root. The probe uses the current local production clients, waits for capability verification, performs a bounded authenticated query and verifies one author signature when a result exists. It writes separate protocol reports, closes the transport and returns a failing exit code for missing directories or query errors. It never publishes content, accesses USB, or modifies server configuration. An empty successful query proves the query path, not publication or replication.

Only that initial connection configuration needs a website deployment. New cache publications and revisions do not. A new directory starts empty with a new identity and cannot automatically recover data whose only source is offline. Existing identity/data migration must be deliberate and performed with the relevant service stopped.

## Certificate renewal

The bridge loads TLS material at startup. DSM renewal alone does not refresh the deployment copy or reload the process. Configure a privileged DSM scheduled task or supported renewal hook to:

1. Select the correct certificate from the actual DSM certificate configuration.
2. Stage the renewed chain and key privately; verify hostname, expiry and key match before replacing anything.
3. Copy both files into the existing deployment `certs` directory with UID/GID 10001 and the permissions above. Keep the mounted directory itself in place; replace its files rather than swapping its directory inode.
4. Once both files are ready, execute `docker compose restart bridge` from the deployment directory.
5. Check bridge health and external TLS. Retain the previous working pair if staging validation fails.

Run this when the certificate changes. Restart briefly interrupts browser sessions. Preserve the renewal task across NAS upgrades; never log private-key contents. The NAS operator must implement the hook using the actual DSM certificate paths, which this repository does not guess.

## Persistence and upgrades

Named volumes `trail-mate-geocaching_directory-data`, `trail-mate-geocaching_track-data` and `trail-mate-geocaching_reticulum-data` retain the two service identities, SQLite/content data, LXMF and Reticulum state. Back up consistently while all services are stopped; identity material is private. Bind-mount replacements require write access for UID 10001. Preserve the administrative catalogue and namespace separately if enabled.

Upgrade the services together because they share a network namespace:

```sh
docker compose down
git pull --ff-only
docker compose build directory
docker compose up -d --no-build --remove-orphans
```

`down` preserves data. **Do not add `-v` or `--volumes`.** Preserve certificate copies and the renewal procedure separately from the directory data volumes.

## Verification boundary

The Compose model is checked locally for exactly one host-port mapping and no tunnel service. Runtime import testing stages exactly the Docker COPY source files outside the repository, including the reply queue and track package. Native shared-instance/browser-gateway acceptance is separate from Docker startup. The development machine's Linux Docker engine is unavailable, so image build, Linux container sharing, permissions and startup still need NAS verification. The bridge's existing TLS options are reused without loosening the Origin policy.

The Geocaching store releases frozen item lists after a complete query/sync while retaining cached cursor bodies and request receipts. It also reclaims legacy completed snapshots during admission/maintenance; live unprocessed continuations keep their original seven-day expiry and are never evicted for another query. Current browsers additionally negotiate the optional capability selector and explicitly release discarded browse queries before viewport replacement or refresh. The caller can only release its own browse snapshot, with durable retry and released-query tombstones. Older clients, unreachable/replaced services or abruptly closed pages can still leave snapshots until expiry. See `site/geocaching/README.md` for the wire extension. Do not interpret these changes as an unlimited visitor-capacity guarantee.

The bridge limit is 32 concurrent clients, not a tested NAS capacity guarantee. Acceptance requires a publication and a later revision appearing in fresh browser queries without website deployment, plus identity/data persistence across service restart.
