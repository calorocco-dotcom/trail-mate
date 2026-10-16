# Deployment with Multiple Entry Points

For Docker Compose with Synology DDNS and TCP 18434, see
[COMPOSE.md](COMPOSE.md) and [compose.yaml](compose.yaml). That setup does not
use Cloudflare or occupy the NAS's ports 80/443.

The Web map automatically connects to the maintained WSS service. One server
Reticulum instance connects to multiple public TCP entrypoints and hosts the
persistent Geocaching directory. Ordinary map visitors do not configure its
server-side interfaces.

```text
Browser RNS/LXMF Worker
        | WSS raw RNS packets
        v
TLS reverse proxy -> packet_bridge.py -> local 127.0.0.1:44242
                                               |
                                Reticulum transport + Geocaching directory
                                  |-- Sydney TCP entrypoint
                                  |-- ReticulumNet NL TCP entrypoint
                                  '-- RMAP TCP entrypoint
```

`reticulum/config` enables native interface discovery with a minimum stamp value
of 16 and up to two automatically connected discovered interfaces. The three
configured TCP clients are bootstrap-only: Reticulum detaches them when the
discovered connection target is reached and restores them if all discovered
connections are lost. Bootstrap connections can overlap during this transition;
the two-interface limit applies to automatically discovered connections only.
The loopback TCP server for the WSS bridge remains enabled independently.

The existing `reticulum-data` volume retains discovered interface state across
container restarts. Keep that volume when updating. The packet bridge does not
implement another routing or failover layer. A lost application link can still
require path rediscovery and a request retry. If no cached or bootstrap endpoint
is reachable, native discovery alone cannot obtain new Internet entrypoints.

Copy `reticulum/` into a private persistent directory on the server and pass it
as `--rns-config`. For example, with the configuration at
`/srv/trail-mate/reticulum/config`, run:

```sh
python tools/geocaching/directory_service.py --state /srv/trail-mate/directory --rns-config /srv/trail-mate/reticulum --name "Trail Mate public directory"
python tools/geocaching/packet_bridge.py --tcp-host 127.0.0.1 --tcp-port 44242 --port 8787 --origin https://vicliu624.github.io
```

Use a TLS reverse proxy with a valid certificate to forward your domain's WSS
requests to `127.0.0.1:8787`. Preserve the WebSocket upgrade, binary messages and
Origin. Set the actual WSS URL as `endpoint` in `site/geocaching/network.json`.
If the website uses a custom domain, update the bridge's `--origin` as well.
Operators manage these deployment settings.

Configured bootstrap defaults are `sydney.reticulum.au:4242`,
`node.reticulumnet.nl:4242` and `rmap.world:4242`. They were selected from
observation records and public-service descriptions; bare IPs backed only by
one successful connectivity check were removed. See
[device network configuration](../../../docs/reticulum_network_config.md)
for the basis and limits of those observations. Observation duration is not
continuous uptime. Local DNS was proxied, and TCP connection success alone
does not prove origin-service availability.

Temporary access verified public-directory discovery and test-point retrieval.
Public-operator failure recovery and long-term availability were not verified.
Multiple upstreams do not imply multiple Geocaching directories or remove the
single bridge server's failure risk. A second bridge and directory replica could
later provide an automatic browser fallback.

## Deployment and verification history

On 2026-09-24, GitHub Pages was published with `network.json` pointing to a
temporary development-machine WSS tunnel and a public directory discovery seed.
The live page queried a test point, verified its author signature and downloaded
GPX. A separate check without intercepting map requests confirmed actual
OpenStreetMap tiles loaded. The point at 0,0 was an integration fixture, not a
physical cache. That temporary service depended on the development machine and
did not constitute persistent-server deployment.

As of 2026-10-14, the existing NAS Compose directory/bridge deployment has been
updated while preserving its identity, epoch and records. Public queries,
author signatures and directory publication proofs pass through the maintained
WSS endpoint. The temporary-tunnel limitation above is historical. Real-Wio
acceptance of the new browser create/update/archive workflow remains pending.
The systemd/Caddy instructions below describe an alternative Linux deployment;
the NAS deployment does not prove those alternative units or TLS settings.

Local multi-interface recovery has a real RNS/LXMF test: after the browser
queries the list, close the selected upstream's TCP forwarder, refuse its
reconnection, and retrieve signed details/GPX through another interface.
Restarting the WSS bridge then allows the original tab to query again without
refreshing or changing settings. Test entrypoint:

```sh
node tests/site/geocaching-browser.mjs --multi-upstream --reconnect --work .codex-build/browser-recovery-run --python /path/to/venv/python
```

Both local upstreams connect to one directory. This proves recovery after an
independent TCP interface fails, not completed failure acceptance across two
public operators, different geographic locations or directory replicas.

## Persistent Linux services

The repository includes `trail-mate-directory.service`,
`trail-mate-bridge.service`, `bridge.env` and `Caddyfile`. This layout is for
Linux servers with systemd, Python 3.10+ and Caddy 2. Administrators own the code
at `/opt/trail-mate`; the service account can write only directory data,
identities and Reticulum state under `/var/lib/trail-mate`.

Validation boundary for the initial setup: file review and `git diff --check`
passed. Local WSL `systemd-analyze verify` could not initialize its manager
because of control-group permissions. Caddy was unavailable locally, and real
TLS configuration had not been verified. The server checks below are deployment
instructions rather than a record of those checks passing.

After installing the code, create the dedicated account and virtual environment
as an administrator:

```sh
sudo useradd --system --home-dir /var/lib/trail-mate --shell /usr/sbin/nologin trail-mate
sudo python3 -m venv /opt/trail-mate/.venv
sudo /opt/trail-mate/.venv/bin/pip install -r /opt/trail-mate/tools/geocaching/interop-requirements.txt -r /opt/trail-mate/tools/geocaching/bridge-requirements.txt
sudo install -d -o trail-mate -g trail-mate -m 0700 /var/lib/trail-mate /var/lib/trail-mate/reticulum
sudo install -d -m 0755 /etc/trail-mate
sudo install -m 0644 /opt/trail-mate/tools/geocaching/deploy/bridge.env /etc/trail-mate/bridge.env
sudo install -m 0644 /opt/trail-mate/tools/geocaching/deploy/trail-mate-directory.service /etc/systemd/system/
sudo install -m 0644 /opt/trail-mate/tools/geocaching/deploy/trail-mate-bridge.service /etc/systemd/system/
```

Copy Reticulum configuration on first installation. Preserve existing
configuration and the entire state directory during upgrades:

```sh
sudo test -e /var/lib/trail-mate/reticulum/config || sudo install -o trail-mate -g trail-mate -m 0600 /opt/trail-mate/tools/geocaching/deploy/reticulum/config /var/lib/trail-mate/reticulum/config
sudo systemd-analyze verify /etc/systemd/system/trail-mate-directory.service /etc/systemd/system/trail-mate-bridge.service
```

Replace `geocaching.example.org` in `Caddyfile` with your real domain, merge it
into `/etc/caddy/Caddyfile`, and preserve existing sites. DNS must point to the
server, with public TCP 80/443 access. Ports 8787 and 44242 stay on loopback and
must not be exposed publicly.

For a custom website domain, first update the exact Origin in
`/etc/trail-mate/bridge.env`. An Origin has no URL path; do not include the
GitHub Pages project path.

```sh
sudo caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile
sudo systemctl daemon-reload
sudo systemctl enable --now trail-mate-directory.service trail-mate-bridge.service
sudo systemctl reload caddy
sudo journalctl -u trail-mate-directory.service -u trail-mate-bridge.service -n 100 --no-pager
```

Both services restart after abnormal exits. Directory restart may interrupt
existing bridge connections, requiring the browser to reconnect, rediscover a
path and retry. systemd startup order does not prove directory readiness. Caddy
handles certificates and WebSocket forwarding. Bridge restart also disconnects
existing clients.

First verify WSS connection, directory discovery, signed details and GPX
download through a real HTTPS page. Then set the deployed endpoint, such as
`wss://your-domain.example/rns`, in `site/geocaching/network.json` and publish
the site.

Set the directory's announced discovery destination in `discoverySeeds`: at
most three 32-character hexadecimal strings. This is the discovery address,
not the LXMF delivery address. After connection/reconnection, the page requests
paths for these seeds while still verifying the same-identity binding between
discovery announces and delivery. Example domains in this repository are not
usable production endpoints.

Device startup discovery seeds live in `geocaching_discovery_probe.h` and must
also contain discovery rather than delivery addresses. The initial device seed
matched the Web integration directory. Preserve the directory identity during
migration to retain its addresses.

When Geocaching is open and TCP is ready, the device actively requests seed
paths, then at most once per minute. Calls, Nomad and screen-saving defer probes.
Retain this refresh interval after an announce so that a callback delayed by a
busy storage lock does not lead to indefinite waiting. Disabling the feature
stops probes. Requests use only IP interfaces and add no LoRa broadcasts.
Seeds assist startup discovery; they do not bypass signature/capability checks
or exclude other directories.

For a new deployment, verify that server restart preserves the directory
identity and published records, and that a single-upstream outage recovers.
The initial alternative Linux setup had not completed these acceptance checks.

Stop the bridge and directory before copying all of `/var/lib/trail-mate` for
backup, then restore service operation. Do not copy only an actively written
SQLite main file and omit its WAL. State contains private keys; keep backups
private.

Stop services when upgrading application code or the virtual environment.
Preserve state, Origin and Caddy configuration. Check database-format
compatibility before rolling code back.

Protocol configuration reference:
<https://reticulum.network/manual/interfaces.html>

## Updating public archive handling

After pulling the revision you intend to deploy, rebuild the NAS application
image and recreate services from the repository root:

```sh
git pull --ff-only
docker compose -f tools/geocaching/deploy/compose.yaml up -d --build
docker compose -f tools/geocaching/deploy/compose.yaml logs --tail=50 directory
```

Keep existing volumes and configuration. This update does not require a new
directory identity, discovery address, WSS address or GitHub Pages deployment.
Updating firmware alone does not update the NAS directory. The updated directory
hides archived caches and rejects old active-version downloads with
`410 cache_archived`; it retains signed archival records for peer synchronization.
